# VitaMediaDeck documentation — 1.4.1

Current app **1.4.1**, Vita package **01.05**, standard bubble **VitaMediaDeck**.
This index covers all maintained Markdown documents in the app repository.
Decoder packages have independent 2.0.0 ABI versions and their own runtime,
performance, validation and change records.

## Start here

- [Podcast resume and lightweight checkpoints](podcast-resume-2026-10-24.md): restart/suspend recovery, storage budget and device checks.

- [Dedicated Jellyfin provider](jellyfin-provider-2026-10-24.md): L1 order, saved servers and video-browser return in all seven themes.

- [Provider navigation and favourite removal](provider-navigation-2026-10-24.md): all seven themes, saved-state consistency and provider controls.

- [Controls clarification follow-up](controls-guide-2026-10-23.md): guide, theme navigation, active mapping and README audit.

- [Release 1.4.1](releases/1.4.1.md): integrated changes, profile and limits.
- [Validation](VALIDATION.md): suite commands, evidence levels and console checklist.
- [Specification](../mds/PROJECT_SPECIFICATION.md): current features and contracts.
- [Controls](../mds/CONTROLS.md): actual context-dependent button/touch mappings.
- [Toolchain](../mds/setup/TOOLCHAIN_SETUP.md), [packages](../mds/PACKAGE_INTEGRATION.md)
  and [release/licensing](../mds/RELEASE_AND_LICENSING_PLAN.md).
- [Theme coverage](theme-coverage-2026-10-23.md) and
  [continuous motion](theme-motion-2026-10-23.md): implementation evidence integrated
  from the motion-1023/theme-cover-1023 test builds.
- [PlayerFix console evidence](console-validation-playerfix-2026-10-23.md):
  previous user tests/logs; not a fresh 1.4.1 device run.

## Documentation policy

Active specification, controls and build instructions describe the current
source. Dated evidence retains original identifiers, values and acceptance
boundaries. Research/design documents describe proposals or external findings
at their recorded date; no additional algorithms/services or new provider-policy
verification are implied by this documentation refresh. Historical 1.4.0 artifacts
and hashes remain unchanged. Tests must not be mistaken for real-console capture.

## Complete inventory

| Document | Scope |
| --- | --- |
| [CONTRIBUTING.md](../CONTRIBUTING.md) | Current reference, policy or build instructions |
| [README.md](../README.md) | Current reference, policy or build instructions |
| [SECURITY.md](../SECURITY.md) | Current reference, policy or build instructions |
| [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) | Current reference, policy or build instructions |
| [assets/flags/NOTICE.md](../assets/flags/NOTICE.md) | Current reference, policy or build instructions |
| [assets/icon_candidates/README.md](../assets/icon_candidates/README.md) | Current reference, policy or build instructions |
| [design/theme-exploration/README.md](../design/theme-exploration/README.md) | Historical concept or research; not release implementation proof |
| [design/theme-exploration/motion.md](../design/theme-exploration/motion.md) | Historical concept or research; not release implementation proof |
| [design/theme-exploration/screen-inventory.md](../design/theme-exploration/screen-inventory.md) | Historical concept or research; not release implementation proof |
| [design/theme-exploration/validation.md](../design/theme-exploration/validation.md) | Historical concept or research; not release implementation proof |
| [docs/VALIDATION.md](VALIDATION.md) | Current reference, policy or build instructions |
| [docs/cache-and-ghost-2026-10-23.md](cache-and-ghost-2026-10-23.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/console-validation-playerfix-2026-10-23.md](console-validation-playerfix-2026-10-23.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/cyberpunk-ui-study.md](cyberpunk-ui-study.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/dash-streaming.md](dash-streaming.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/decoder-performance-research-2026-10-23.md](decoder-performance-research-2026-10-23.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/features-and-services.md](features-and-services.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/game-ui-2026-10-23.md](game-ui-2026-10-23.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/iptv-availability.md](iptv-availability.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/iptv-plan.md](iptv-plan.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/online-audio-preview.md](online-audio-preview.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/personal-media-2026-10-23.md](personal-media-2026-10-23.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/player-fixes-2026-10-23.md](player-fixes-2026-10-23.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/quality-audit-2026-10-22.md](quality-audit-2026-10-22.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/releases/1.4.0.md](releases/1.4.0.md) | Archived release; retain original metadata and evidence |
| [docs/releases/1.4.1.md](releases/1.4.1.md) | Current standard release and reproduction |
| [docs/scene-themes-2026-10-23.md](scene-themes-2026-10-23.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/theme-chrome-2026-10-23.md](theme-chrome-2026-10-23.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/theme-coverage-2026-10-23.md](theme-coverage-2026-10-23.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/theme-motion-2026-10-23.md](theme-motion-2026-10-23.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/themes-2026-10-22.md](themes-2026-10-22.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/themes-navigation-2026-10-23.md](themes-navigation-2026-10-23.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/themes-player-feedback-2026-10-23.md](themes-player-feedback-2026-10-23.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/ui-feedback-2026-10-22.md](ui-feedback-2026-10-22.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/ui-refinement.md](ui-refinement.md) | Dated implementation/research snapshot; current integration linked above it |
| [docs/ui-style-research-2026-10-22.md](ui-style-research-2026-10-22.md) | Dated implementation/research snapshot; current integration linked above it |
| [licenses/REAVPLAYER_PROVENANCE.md](../licenses/REAVPLAYER_PROVENANCE.md) | Current reference, policy or build instructions |
| [mds/CONTROLS.md](../mds/CONTROLS.md) | Current reference, policy or build instructions |
| [mds/DEVELOPMENT_LOG.md](../mds/DEVELOPMENT_LOG.md) | Current reference, policy or build instructions |
| [mds/H264_ACCELERATION_RESEARCH_PLAN.md](../mds/H264_ACCELERATION_RESEARCH_PLAN.md) | Historical research/audit; latest measured evidence linked from validation |
| [mds/PACKAGE_INTEGRATION.md](../mds/PACKAGE_INTEGRATION.md) | Current reference, policy or build instructions |
| [mds/PLAYBACK_PERFORMANCE_AUDIT.md](../mds/PLAYBACK_PERFORMANCE_AUDIT.md) | Historical research/audit; latest measured evidence linked from validation |
| [mds/PROJECT_SPECIFICATION.md](../mds/PROJECT_SPECIFICATION.md) | Current reference, policy or build instructions |
| [mds/RELEASE_AND_LICENSING_PLAN.md](../mds/RELEASE_AND_LICENSING_PLAN.md) | Current reference, policy or build instructions |
| [mds/UI_SIGNAL_SHELL.md](../mds/UI_SIGNAL_SHELL.md) | Current reference, policy or build instructions |
| [mds/research/BACKGROUND_PLAYBACK_VITA.md](../mds/research/BACKGROUND_PLAYBACK_VITA.md) | Historical concept or research; not release implementation proof |
| [mds/research/PSVITA_HARDWARE_GPU_RESOURCES.md](../mds/research/PSVITA_HARDWARE_GPU_RESOURCES.md) | Historical concept or research; not release implementation proof |
| [mds/setup/TOOLCHAIN_SETUP.md](../mds/setup/TOOLCHAIN_SETUP.md) | Current reference, policy or build instructions |
| [src/common/README.md](../src/common/README.md) | Current reference, policy or build instructions |
| [tests/home_tabs/README.md](../tests/home_tabs/README.md) | Executable suite guide; host/device limits stated locally |
| [tests/image_viewer/README.md](../tests/image_viewer/README.md) | Executable suite guide; host/device limits stated locally |
| [tests/iptv/README.md](../tests/iptv/README.md) | Executable suite guide; host/device limits stated locally |
| [tests/online_audio/README.md](../tests/online_audio/README.md) | Executable suite guide; host/device limits stated locally |
| [tests/online_navigation/README.md](../tests/online_navigation/README.md) | Executable suite guide; host/device limits stated locally |
| [tests/personal_media/README.md](../tests/personal_media/README.md) | Executable suite guide; host/device limits stated locally |
| [tests/playback_seek/README.md](../tests/playback_seek/README.md) | Executable suite guide; host/device limits stated locally |
| [tests/player_feedback/README.md](../tests/player_feedback/README.md) | Executable suite guide; host/device limits stated locally |
| [tests/podcast_metadata/README.md](../tests/podcast_metadata/README.md) | Executable suite guide; host/device limits stated locally |
| [tests/ui_coverage/README.md](../tests/ui_coverage/README.md) | Executable suite guide; host/device limits stated locally |
| [tests/ui_crossfade/README.md](../tests/ui_crossfade/README.md) | Executable suite guide; host/device limits stated locally |
| [tools/local_streaming/README.md](../tools/local_streaming/README.md) | Current reference, policy or build instructions |
| [docs/controls-guide-2026-10-23.md](controls-guide-2026-10-23.md) | Current controls clarification following the 1.4.1 standard snapshot |
| [docs/jellyfin-provider-2026-10-24.md](jellyfin-provider-2026-10-24.md) | Dedicated Jellyfin L1 provider and validation limits |
| [docs/podcast-resume-2026-10-24.md](podcast-resume-2026-10-24.md) | Podcast checkpoint reliability, I/O budget and acceptance limits |
| [tests/podcast_resume/README.md](../tests/podcast_resume/README.md) | Persistence/worker/real MP3 resume host regressions |
