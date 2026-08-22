#!/usr/bin/env bash
# =============================================================================
#  build-rt-kernel.sh
#  Cross-Compile eines PREEMPT_RT-Kernels fuer Raspberry Pi 4 (BCM2711, arm64)
#
#  Kernpunkt: Es wird KEIN RT-Patch heruntergeladen und angewendet.
#  Seit Kernel 6.12 ist PREEMPT_RT in mainline. Der Raspberry-Pi-Kernel-Tree
#  liefert dafuer die fertige Konfiguration mit:
#       arch/arm64/configs/bcm2711_rt_defconfig
#  Verifiziert vorhanden auf rpi-6.12.y, rpi-6.16.y und rpi-6.18.y.
#
#  Ausfuehren auf dem ENTWICKLUNGSRECHNER (Docker noetig), nicht auf dem Pi.
#
#  Nutzung:
#      ./build-rt-kernel.sh
#      KERNEL_BRANCH=rpi-6.12.y ./build-rt-kernel.sh     # Fallback
#      JOBS=8 ./build-rt-kernel.sh
# =============================================================================
set -Eeuo pipefail

KERNEL_BRANCH="${KERNEL_BRANCH:-rpi-6.18.y}"
DEFCONFIG="${DEFCONFIG:-bcm2711_rt_defconfig}"
KERNEL_IMAGE_NAME="${KERNEL_IMAGE_NAME:-kernel8-rt.img}"
JOBS="${JOBS:-$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) )}"
WORKDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTDIR="${WORKDIR}/out"
BUILDDIR="${WORKDIR}/build"

log()  { printf '\033[1;34m[BUILD]\033[0m %s\n' "$*"; }
inf()  { printf '\033[1;36m[INFO]\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m[  OK ]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[FEHLER]\033[0m %s\n' "$*" >&2; exit 1; }

trap 'die "Abbruch in Zeile $LINENO."' ERR

# ---------------------------------------------------------------- Vorbedingung
command -v docker >/dev/null 2>&1 || die "Docker nicht gefunden. Bitte installieren."
docker info >/dev/null 2>&1 || die "Docker-Daemon laeuft nicht."

mkdir -p "$OUTDIR" "$BUILDDIR"

log "Branch          : ${KERNEL_BRANCH}"
log "Defconfig       : ${DEFCONFIG}"
log "Parallele Jobs  : ${JOBS}"
log "Ausgabe         : ${OUTDIR}"
echo

# ------------------------------------------------- Defconfig vorab verifizieren
log "Pruefe, ob ${DEFCONFIG} auf ${KERNEL_BRANCH} existiert ..."
DEFCONFIG_URL="https://raw.githubusercontent.com/raspberrypi/linux/${KERNEL_BRANCH}/arch/arm64/configs/${DEFCONFIG}"
# WICHTIG: Datei EINMAL herunterladen und dann pruefen.
# Nicht "curl ... | grep -q" verwenden: grep -q beendet sich beim ersten
# Treffer, curl bekommt SIGPIPE und liefert Exit 23 -- und wegen
# "set -o pipefail" schlaegt die gesamte Pipeline fehl, OBWOHL der Treffer
# da war. Genau dieser Fehler hat frueher faelschlich gemeldet, die
# Defconfig enthalte kein CONFIG_PREEMPT_RT.
DEFCONFIG_TMP="$(mktemp)"
trap 'rm -f "$DEFCONFIG_TMP"' EXIT

HTTP_CODE="$(curl -sS -L -o "$DEFCONFIG_TMP" -w '%{http_code}' "$DEFCONFIG_URL" || echo 000)"
[ "$HTTP_CODE" = "200" ] || die "${DEFCONFIG} auf ${KERNEL_BRANCH} nicht gefunden (HTTP ${HTTP_CODE}).
Pruefe Branch-Name oder nutze: KERNEL_BRANCH=rpi-6.12.y $0"
[ -s "$DEFCONFIG_TMP" ] || die "Defconfig wurde leer heruntergeladen. Netzwerk pruefen."
ok "Defconfig gefunden ($(wc -l < "$DEFCONFIG_TMP") Zeilen)."

# Sanity: enthaelt sie wirklich PREEMPT_RT?
grep -q '^CONFIG_PREEMPT_RT=y' "$DEFCONFIG_TMP" \
    || die "Defconfig enthaelt kein CONFIG_PREEMPT_RT=y. Abbruch (das waere kein RT-Kernel)."
ok "CONFIG_PREEMPT_RT=y bestaetigt."
inf "Weitere relevante Optionen:"
grep -E '^CONFIG_(LOCALVERSION|HIGH_RES_TIMERS|TIMERLAT_TRACER|OSNOISE_TRACER)=' \
    "$DEFCONFIG_TMP" | sed 's/^/         /' || true
echo

# --------------------------------------------------------------- Build-Container
cat > "${BUILDDIR}/Dockerfile" <<'DOCKERFILE'
FROM debian:bookworm-slim
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential \
      crossbuild-essential-arm64 \
      git bc bison flex make libc6-dev libncurses-dev \
      libssl-dev libelf-dev dwarves \
      ca-certificates kmod cpio rsync xz-utils zstd python3 \
 && rm -rf /var/lib/apt/lists/*

# Selbsttest beim Container-Bau: beide Compiler MUESSEN vorhanden sein.
# Ein Kernel-Build braucht zwei davon:
#   gcc                     -> HOSTCC, baut Hilfsprogramme (scripts/basic/fixdep)
#                              die auf dem Build-Rechner laufen
#   aarch64-linux-gnu-gcc   -> CROSS_COMPILE, baut den Kernel fuer ARM64
# crossbuild-essential-arm64 liefert NUR den zweiten. Ohne build-essential
# scheitert der Build mit "gcc: not found" bei scripts/basic/fixdep.
RUN gcc --version && aarch64-linux-gnu-gcc --version && ld --version | head -1
WORKDIR /work
DOCKERFILE

log "Baue Build-Container (einmalig, danach gecacht) ..."
docker build -q -t rpi-rt-builder "${BUILDDIR}" >/dev/null
ok "Container bereit."
echo

# --------------------------------------------------------------- Build-Skript
cat > "${BUILDDIR}/inner-build.sh" <<'INNER'
#!/usr/bin/env bash
set -Eeuo pipefail

BRANCH="$1"; DEFCONFIG="$2"; JOBS="$3"; IMGNAME="$4"

export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-

cd /work

if [ ! -d linux/.git ]; then
    echo ">>> Klone raspberrypi/linux (${BRANCH}), shallow ..."
    git clone --depth=1 --branch "$BRANCH" https://github.com/raspberrypi/linux.git linux
else
    echo ">>> Bestehender Klon gefunden, aktualisiere ..."
    cd linux
    git fetch --depth=1 origin "$BRANCH"
    git checkout -f FETCH_HEAD
    cd /work
fi

cd linux
echo ">>> Kernel-Version: $(make kernelversion 2>/dev/null || echo unbekannt)"

echo ">>> make ${DEFCONFIG}"
make "$DEFCONFIG"

# Verifikation NACH der Konfiguration -- das ist die Stelle, an der ein
# falscher defconfig sonst unbemerkt durchrutschen wuerde.
grep -q '^CONFIG_PREEMPT_RT=y' .config \
    || { echo "FEHLER: .config enthaelt kein CONFIG_PREEMPT_RT=y"; exit 1; }
echo ">>> Bestaetigt: CONFIG_PREEMPT_RT=y"
grep -E '^CONFIG_(LOCALVERSION|HIGH_RES_TIMERS|TIMERLAT_TRACER|OSNOISE_TRACER)=' .config || true

echo ">>> Kompiliere mit -j${JOBS} (das dauert 20-40 Minuten) ..."
make -j"${JOBS}" Image.gz modules dtbs

echo ">>> Installiere Module in Staging-Verzeichnis ..."
rm -rf /work/staging
mkdir -p /work/staging
make -j"${JOBS}" INSTALL_MOD_PATH=/work/staging modules_install

RELEASE="$(cat include/config/kernel.release)"
echo ">>> Kernel-Release: ${RELEASE}"

mkdir -p /work/out
cp arch/arm64/boot/Image.gz "/work/out/${IMGNAME}"
echo "${RELEASE}" > /work/out/rt-kernel-release.txt

echo ">>> Packe Module ..."
tar -C /work/staging -czf /work/out/rt-modules.tar.gz lib/modules

echo ">>> Packe Device Trees und Overlays ..."
rm -rf /work/dtstage && mkdir -p /work/dtstage/overlays
cp arch/arm64/boot/dts/broadcom/*.dtb            /work/dtstage/            2>/dev/null || true
cp arch/arm64/boot/dts/overlays/*.dtb*           /work/dtstage/overlays/   2>/dev/null || true
cp arch/arm64/boot/dts/overlays/README           /work/dtstage/overlays/   2>/dev/null || true
tar -C /work/dtstage -czf /work/out/rt-dtbs.tar.gz .

echo ">>> Fertig."
INNER
chmod +x "${BUILDDIR}/inner-build.sh"

log "Starte Kernel-Build. Jetzt ist Zeit fuer etwas anderes."
log "Erwartete Dauer: 20-40 Minuten."
echo

START=$(date +%s)
docker run --rm \
    -v "${BUILDDIR}:/work" \
    -v "${OUTDIR}:/work/out" \
    rpi-rt-builder \
    /work/inner-build.sh "$KERNEL_BRANCH" "$DEFCONFIG" "$JOBS" "$KERNEL_IMAGE_NAME"
END=$(date +%s)

echo
ok "Build abgeschlossen in $(( (END-START)/60 )) Minuten $(( (END-START)%60 )) Sekunden."
echo
echo "  Kernel-Release : $(cat "${OUTDIR}/rt-kernel-release.txt")"
echo "  Kernel-Image   : ${OUTDIR}/${KERNEL_IMAGE_NAME}  ($(du -h "${OUTDIR}/${KERNEL_IMAGE_NAME}" | cut -f1))"
echo "  Module         : ${OUTDIR}/rt-modules.tar.gz     ($(du -h "${OUTDIR}/rt-modules.tar.gz" | cut -f1))"
echo "  Device Trees   : ${OUTDIR}/rt-dtbs.tar.gz        ($(du -h "${OUTDIR}/rt-dtbs.tar.gz" | cut -f1))"
echo
echo "Naechster Schritt:"
echo "  scp ${OUTDIR}/${KERNEL_IMAGE_NAME} ${OUTDIR}/rt-modules.tar.gz ${OUTDIR}/rt-dtbs.tar.gz \\"
echo "      ${OUTDIR}/rt-kernel-release.txt install-rt-kernel.sh gaetan@rt-lab.local:~/"
