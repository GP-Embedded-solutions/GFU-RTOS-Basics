# Real-Time Linux — Fundamentals and Practical Applications

Course repository. 3-day live-online seminar, trainer **Gaëtan Pandji**.
Target hardware: **Raspberry Pi 4 Model B (4 GB, BCM2711, arm64)**.

---

## Start here

| Order | Do this | When |
|---|---|---|
| 1 | Read `docs/01_Raspberry_Pi_OS_Download_and_Flash.pdf` and follow it end to end | **Before day 1** |
| 2 | Run `scripts/verify.sh` on the Pi — all mandatory checks must pass | Before day 1 |
| 3 | Report the output in the course channel if anything fails | Before day 1 |
| 4 | `docs/02_PREEMPT_RT_Setup_on_Raspberry_Pi_4.pdf` | Day 1, guided or Bedfore day 1 |

Do not start document 2 until `verify.sh` passes. Debugging a custom kernel on
top of a broken base wastes an evening.

---

## Layout

```
.
├── README.md                 this file
├── docs/
│   ├── 01_Raspberry_Pi_OS_Download_and_Flash.pdf
│   └── 02_PREEMPT_RT_Setup_on_Raspberry_Pi_4.pdf
├── scripts/
│   ├── provision-pi.sh       baseline packages, I2C, locale
│   ├── verify.sh             automated checklist from both documents
│   ├── build-rt-kernel.sh    cross-compile PREEMPT_RT (run on your PC)
│   ├── switch-kernel.sh      select stock/RT kernel, toggle CPU isolation (run on the Pi)
│   └── latency_showdown.sh   day 1 demo: stock kernel vs. PREEMPT_RT
├── config/
│   └── rt-v8.config          the exact kernel .config the build produces
├── exercises/                AUFGABE_Stufe1.md, stage1_naive.c, stage1_reference.c
├── results/                  commit your own cyclictest histograms here
└── DEFINITION_OF_DONE.md     acceptance criteria per project stage
```

## Quick reference

```bash
# On the Pi
git clone <repository-url> ~/rt-linux-seminar
sudo ~/rt-linux-seminar/scripts/provision-pi.sh
~/rt-linux-seminar/scripts/verify.sh

sudo ./scripts/switch-kernel.sh status        # what am I running?
sudo ./scripts/switch-kernel.sh rt            # boot the RT kernel next time
sudo ./scripts/switch-kernel.sh stock         # go back
sudo ./scripts/switch-kernel.sh isolate on    # isolcpus/nohz_full/rcu_nocbs/irqaffinity

# On your PC
./scripts/build-rt-kernel.sh rpi-6.18.y
```

## If the Pi will not boot

Nothing is lost. Power off, put the microSD card in a reader on your PC, open
`config.txt` on the small FAT partition and comment out the `kernel=` line.
Check `cmdline.txt` is still exactly one line. Boot. You are on the stock kernel
with your files intact. Full procedure: document 2, section 11.

## Network fallback

The standard path is SSH from your own machine to the Pi on your LAN. If mDNS is
blocked (guest Wi-Fi, client isolation, corporate VLAN), use the IP address from
your router. If that also fails, Tailscale is pre-installed but dormant on the
image — ask the trainer to activate it. There is **no** permanent remote access
to your device.

## Git workflow

Branches: `stage/1`, `stage/2`, `stage/3` for the project stages;
`fix/<short-description>` for corrections. Pull requests for anything touching
`docs/` or `scripts/`. Using this workflow properly is itself part of the course.

## Reporting problems

If a command in the documentation fails on your hardware, that is a defect in the
documentation. Open an issue with: the command, the full output, `uname -a`, and
your image build date.

## Facts and versions

Documentation is current as of **19 August 2026** and targets Raspberry Pi OS
build **2026-06-18** (Debian 13 Trixie, Linux **6.18.34**).

Note that **PREEMPT_RT has been part of the mainline Linux kernel since 6.12**.
Most tutorials online predate that and tell you to download and apply an `rt`
patch. On 6.12 and later that step does not exist. If a guide says
`wget patch-*-rt*.patch`, it is describing an older kernel.

Raspberry Pi re-releases images without changing the version number — always read
the SHA-256 from the live download page rather than trusting a cached document.
