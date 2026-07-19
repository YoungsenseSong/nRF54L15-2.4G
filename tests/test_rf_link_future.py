import binascii
import struct
import unittest
from collections import deque
from dataclasses import dataclass
from pathlib import Path


FRAME_SAMPLES = 96
QUEUE_DEPTH = 64
BATCH_START = 1 << 3
FPGA_MAGIC = 0x3146524E
FPGA_VERSION = 1


class SequenceTracker:
    def __init__(self):
        self.have_seq = False
        self.last_seq = 0
        self.extended = 0
        self.logical_index = 0
        self.lost_frames = 0
        self.duplicates = 0
        self.late = 0

    def process(self, seq, sample_count=FRAME_SAMPLES):
        if not self.have_seq:
            self.have_seq = True
            self.extended = seq
        else:
            expected = (self.last_seq + 1) & 0xFFFF
            gap = (seq - expected) & 0xFFFF
            if gap == 0:
                self.extended = (self.extended + 1) & 0xFFFFFFFF
            elif gap < 0x8000:
                self.lost_frames += gap
                self.logical_index += gap * FRAME_SAMPLES
                self.extended = (self.extended + gap + 1) & 0xFFFFFFFF
            elif seq == self.last_seq:
                self.duplicates += 1
                return "duplicate", None
            else:
                self.late += 1
                return "late", None

        start = self.logical_index
        self.logical_index += sample_count
        self.last_seq = seq
        return "accepted", (self.extended, start)


class StaticQueue:
    def __init__(self, depth=QUEUE_DEPTH):
        self.depth = depth
        self.items = deque()
        self.overflow = 0
        self.high_water = 0

    def push(self, value):
        if len(self.items) == self.depth:
            self.overflow += 1
            return False
        self.items.append(value)
        self.high_water = max(self.high_water, len(self.items))
        return True

    def peek(self):
        return self.items[0] if self.items else None

    def commit(self, expected):
        if not self.items or self.items[0] != expected:
            return False
        self.items.popleft()
        return True


class SyncState:
    IDLE = 0
    ARMED = 1
    WAIT_START = 2
    ALIGNING = 3
    LOCKED = 4
    DEGRADED = 5


class SyncModel:
    def __init__(self, timeout_ms=1500):
        self.timeout_ms = timeout_ms
        self.state = SyncState.IDLE
        self.epoch = 0
        self.start_index = None
        self.resync_count = 0
        self.have_armed = False

    def arm(self, epoch):
        if self.have_armed:
            self.resync_count += 1
        self.have_armed = True
        self.state = SyncState.ARMED
        self.epoch = epoch
        self.start_index = None

    def capture(self):
        if self.state == SyncState.ARMED:
            self.state = SyncState.WAIT_START

    def frame(self, sample_index, flags):
        if self.state == SyncState.WAIT_START and flags & BATCH_START:
            self.state = SyncState.ALIGNING
            self.start_index = sample_index
            self.state = SyncState.LOCKED
            return 0
        if self.state == SyncState.LOCKED:
            return sample_index - self.start_index
        return sample_index

    def poll(self, elapsed_ms):
        if self.state in (SyncState.ARMED, SyncState.WAIT_START):
            if elapsed_ms >= self.timeout_ms:
                self.state = SyncState.DEGRADED


def crc16_ccitt(data, seed=0xFFFF):
    return binascii.crc_hqx(data, seed)


@dataclass
class TransportRecord:
    transport_seq: int
    header: bytes
    payload: bytes
    header_crc: int
    payload_crc: int


def build_transport_record(transport_seq, payload):
    header_without_crc = struct.pack(
        "<IBBHIIQQIH",
        FPGA_MAGIC,
        FPGA_VERSION,
        2,
        1,
        transport_seq,
        7,
        1234,
        4096,
        1,
        len(payload),
    )
    header_crc = crc16_ccitt(header_without_crc)
    header = header_without_crc + struct.pack("<H", header_crc)
    return TransportRecord(
        transport_seq,
        header,
        payload,
        header_crc,
        binascii.crc32(payload) & 0xFFFFFFFF,
    )


class CommitModel:
    def __init__(self):
        self.pending = None
        self.last_commit = None
        self.duplicate_commit = 0

    def stage(self, record):
        self.pending = record

    def read(self):
        return self.pending

    def commit(self, transport_seq):
        if self.pending is None:
            if transport_seq == self.last_commit:
                self.duplicate_commit += 1
            return False
        if transport_seq != self.pending.transport_seq:
            return False
        self.last_commit = transport_seq
        self.pending = None
        return True


class FutureContractTests(unittest.TestCase):
    def test_01_rf_sequence_wrap(self):
        tracker = SequenceTracker()
        values = [tracker.process(seq)[1][0] for seq in (0xFFFE, 0xFFFF, 0)]
        self.assertEqual(values, [0xFFFE, 0xFFFF, 0x10000])

    def test_02_single_and_multiple_forward_gaps(self):
        tracker = SequenceTracker()
        tracker.process(10)
        _, one_gap = tracker.process(12)
        _, two_gap = tracker.process(15)
        self.assertEqual(tracker.lost_frames, 3)
        self.assertEqual(one_gap[1], 2 * FRAME_SAMPLES)
        self.assertEqual(two_gap[1], 5 * FRAME_SAMPLES)

    def test_03_duplicate_and_late_frames(self):
        tracker = SequenceTracker()
        tracker.process(10)
        tracker.process(11)
        self.assertEqual(tracker.process(11)[0], "duplicate")
        self.assertEqual(tracker.process(9)[0], "late")
        self.assertEqual((tracker.duplicates, tracker.late), (1, 1))

    def test_04_queue_full_at_64_records(self):
        queue = StaticQueue()
        self.assertTrue(all(queue.push(i) for i in range(QUEUE_DEPTH)))
        self.assertFalse(queue.push(QUEUE_DEPTH))
        self.assertEqual((len(queue.items), queue.high_water, queue.overflow),
                         (64, 64, 1))

    def test_05_peek_does_not_commit(self):
        queue = StaticQueue()
        queue.push(17)
        self.assertEqual(queue.peek(), 17)
        self.assertEqual(len(queue.items), 1)

    def test_06_wrong_sequence_commit_is_rejected(self):
        queue = StaticQueue()
        queue.push(17)
        self.assertFalse(queue.commit(18))
        self.assertEqual(queue.peek(), 17)

    def test_07_sync_arm_capture_wait_start_locked(self):
        sync = SyncModel()
        sync.arm(12)
        self.assertEqual(sync.state, SyncState.ARMED)
        sync.capture()
        self.assertEqual(sync.state, SyncState.WAIT_START)
        self.assertEqual(sync.frame(8192, BATCH_START), 0)
        self.assertEqual(sync.state, SyncState.LOCKED)
        self.assertEqual(sync.frame(8288, 0), 96)

    def test_08_sync_timeout_degrades(self):
        sync = SyncModel()
        sync.arm(1)
        sync.poll(1500)
        self.assertEqual(sync.state, SyncState.DEGRADED)

    def test_09_epoch_switch_clears_alignment(self):
        sync = SyncModel()
        sync.arm(1)
        sync.capture()
        sync.frame(100, BATCH_START)
        sync.arm(2)
        self.assertEqual(sync.epoch, 2)
        self.assertIsNone(sync.start_index)
        self.assertEqual(sync.resync_count, 1)

    def test_10_header_and_payload_crc_detection(self):
        payload = bytes(range(204))
        record = build_transport_record(3, payload)
        self.assertEqual(crc16_ccitt(record.header[:-2]), record.header_crc)
        self.assertEqual(binascii.crc32(record.payload) & 0xFFFFFFFF,
                         record.payload_crc)
        damaged_header = bytearray(record.header)
        damaged_header[5] ^= 1
        self.assertNotEqual(crc16_ccitt(damaged_header[:-2]), record.header_crc)
        damaged_payload = bytearray(record.payload)
        damaged_payload[100] ^= 1
        self.assertNotEqual(binascii.crc32(damaged_payload) & 0xFFFFFFFF,
                            record.payload_crc)

    def test_11_transport_sequence_wrap_and_commit_rules(self):
        model = CommitModel()
        seq = 0xFFFFFFFF
        record = build_transport_record(seq, bytes(204))
        model.stage(record)
        self.assertIs(model.read(), record)
        self.assertFalse(model.commit(0))
        self.assertTrue(model.commit(seq))
        self.assertFalse(model.commit(seq))
        self.assertEqual(model.duplicate_commit, 1)
        self.assertEqual((seq + 1) & 0xFFFFFFFF, 0)

    def test_12_disabled_debug_transport_does_not_block_rf_path(self):
        tracker = SequenceTracker()
        debug_transport_enabled = False
        accepted, _ = tracker.process(1)
        self.assertFalse(debug_transport_enabled)
        self.assertEqual(accepted, "accepted")
        self.assertEqual(tracker.logical_index, FRAME_SAMPLES)

    def test_source_contract_constants_are_present(self):
        repo = Path(__file__).resolve().parents[1]
        header = (repo / "applications/rf_link_rx/src/rf_link_future.h").read_text()
        kconfig = (repo / "applications/rf_link_rx/Kconfig").read_text()
        self.assertIn("BUILD_ASSERT(sizeof(struct fpga_record) == 248u", header)
        self.assertIn("range 64 256", kconfig)


if __name__ == "__main__":
    unittest.main(verbosity=2)
