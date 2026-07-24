# Sourced helper: sets PPC_IMAGE_DEFAULT based on host CPU arch, unless the
# caller has already set PPC_IMAGE. Used by build-bridge-ppc.sh,
# build-example-ppc.sh, and install-toolchains.sh so all three pick the
# same tag on any host without hardcoding arm64.

if [ -z "${PPC_IMAGE:-}" ]; then
    case "$(uname -m)" in
        arm64|aarch64) PPC_IMAGE_DEFAULT="walkero/amigagccondocker:os4-gcc11-arm64" ;;
        x86_64|amd64)  PPC_IMAGE_DEFAULT="walkero/amigagccondocker:os4-gcc11-amd64" ;;
        *)             PPC_IMAGE_DEFAULT="walkero/amigagccondocker:os4-gcc11-arm64" ;;
    esac
    PPC_IMAGE="$PPC_IMAGE_DEFAULT"
fi
