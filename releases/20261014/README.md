# Current Paired nRF Project

`single_channel/` contains the custom TX CPUAPP/FLPR, repaired CH0 RX,
shared protocol, board definitions, build/program entry points and images.

RX repairs cover 260-byte recovery transactions, SPIS scheduling priority
and the pending/queue-peek concurrency window. RX clean build reproduces
SHA256 `0a84ed92aae03f9655c91440bda5f77d14bd8a8178fb00ff4f829fe3ee667216`.

TX source and retained images are the frozen custom-board handoff.
Independent CPUAPP rebuilding omits the former repository boot-banner
commit string; retained images were not overwritten. No new board flashing
or runtime verification was performed during this workspace release.

NCS v3.1.0 and Zephyr SDK 0.17.1 are external prerequisites. The SDK workspace
must register the supplied Connect Kit board module. Build scripts isolate
outputs and create/remove only a temporary SDK junction for Windows paths.

No multichannel RF/node configuration is claimed complete.
