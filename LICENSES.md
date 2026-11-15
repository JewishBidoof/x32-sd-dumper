# Licensing and source obligations

Original utility code, scripts, inspector and documentation in this repository are distributed under GPL-3.0-or-later (see LICENSE). The device tree retains its GPL-2.0+ notice and Freescale copyright; see COPYING.GPL-2.0. The U-Boot patch modifies GPL upstream code and retains the upstream licensing context. No manufacturer firmware or decompiled code is included.

The build downloads OpenX32 at fd94919c1b2fbed21c7ff8e51ca316abf37dcf5f, U-Boot at 050acee119b3757fee3bd128f55d720fdd9bb890, Linux at a256b1e6892e7fe840f0f9746316fa938e9a421f, and optionally BusyBox at 5f07327251c93184dfcfc8d978fc35705930ec53. Public upstream URLs and exact revisions are in scripts/build-in-container.sh under usb-dumper/. The DCP compiler is downloaded as part of pinned OpenX32, not vendored here.

OpenX32 is GPL-3.0; U-Boot and BusyBox use GPL-2.0-or-later; Linux uses GPL-2.0-only with per-file exceptions. Their individual notices and licenses remain authoritative. A boot container aggregates separately licensed programs; this repository license does not relicense them.

This publication distributes source only. Before distributing any built runner, provide its complete corresponding source, exact configurations, patches, build scripts and license notices in the form required by each component license. Merely linking upstream or publishing this utility repository is not necessarily sufficient. Audit all included upstream components and retain applicable notices. No binary release or corresponding-source bundle is supplied here.
