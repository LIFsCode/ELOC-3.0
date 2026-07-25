#
# Reapplies the ELOC reusable-KissFFT-plan optimization after an Edge Impulse
# model export replaces lib/edge-impulse/src/edge-impulse-sdk/dsp/numpy.hpp.
#
# This is a project-local patch. It does not modify the shared PlatformIO
# packages under ~/.platformio.
#
# Safety properties:
# - Idempotent: the ELOC-FFT-CACHE sentinel makes an already-patched file a no-op.
# - Fail-closed: an unpatched file must contain exactly the SDK implementation
#   this patch was verified against. A changed future SDK stops the build.
# - Newline-preserving: LF and CRLF exports are both supported.
#

import os
import sys
import tempfile


SCRIPT_NAME = "patch_ei_fft_cache.py"
SENTINEL = "// ELOC-FFT-CACHE"

ORIGINAL_BLOCK = """        // create fftr context
        size_t kiss_fftr_mem_length;

        kiss_fftr_cfg cfg = kiss_fftr_alloc(n_fft, 0, NULL, NULL, &kiss_fftr_mem_length);
        if (!cfg) {
            EIDSP_ERR(EIDSP_OUT_OF_MEM);
        }

        ei_dsp_register_alloc(kiss_fftr_mem_length, cfg);

        // execute the rfft operation
        kiss_fftr(cfg, fft_input, (kiss_fft_cpx*)output);

        ei_dsp_free(cfg, kiss_fftr_mem_length);
"""

PATCHED_BLOCK = """        // ELOC-FFT-CACHE
        // Building a KissFFT plan calculates all twiddle factors. MFE calls this
        // function once per spectrogram frame, so keep the plan for subsequent
        // frames and inferences instead of rebuilding it every time. The ELOC
        // inference pipeline calls this serially; the plan owns a mutable tmpbuf.
        static kiss_fftr_cfg cached_cfg = NULL;
        static size_t cached_n_fft = 0;

        if (!cached_cfg || cached_n_fft != n_fft) {
            if (cached_cfg) {
                kiss_fftr_free(cached_cfg);
                cached_cfg = NULL;
                cached_n_fft = 0;
            }

            cached_cfg = kiss_fftr_alloc(n_fft, 0, NULL, NULL);
            if (!cached_cfg) {
                EIDSP_ERR(EIDSP_OUT_OF_MEM);
            }

            cached_n_fft = n_fft;
        }

        // execute the rfft operation
        kiss_fftr(cached_cfg, fft_input, (kiss_fft_cpx*)output);
"""

PATCHED_REQUIRED_LINES = (
    "static kiss_fftr_cfg cached_cfg = NULL;",
    "static size_t cached_n_fft = 0;",
    "cached_cfg = kiss_fftr_alloc(n_fft, 0, NULL, NULL);",
    "kiss_fftr(cached_cfg, fft_input, (kiss_fft_cpx*)output);",
)


class PatchError(RuntimeError):
    pass


def patch_numpy_file(numpy_path):
    if not os.path.isfile(numpy_path):
        raise PatchError("numpy.hpp not found at expected location: %s" % numpy_path)

    with open(numpy_path, "rb") as f:
        raw_content = f.read()

    try:
        content = raw_content.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise PatchError("numpy.hpp is not valid UTF-8: %s" % exc)

    if SENTINEL in content:
        missing = [line for line in PATCHED_REQUIRED_LINES if line not in content]
        if missing:
            raise PatchError(
                "%s exists but the cached-plan implementation is incomplete; missing: %s"
                % (SENTINEL, ", ".join(missing))
            )
        print("%s: numpy.hpp already patched (%s)" % (SCRIPT_NAME, SENTINEL))
        return False

    newline = "\r\n" if "\r\n" in content else "\n"
    original = ORIGINAL_BLOCK.replace("\n", newline)
    replacement = PATCHED_BLOCK.replace("\n", newline)
    match_count = content.count(original)

    if match_count != 1:
        raise PatchError(
            "expected exactly one verified unpatched software_rfft block in %s, found %d. "
            "The Edge Impulse SDK implementation may have changed; inspect software_rfft() "
            "and update this patch deliberately instead of applying it blindly."
            % (numpy_path, match_count)
        )

    patched = content.replace(original, replacement, 1)
    with open(numpy_path, "wb") as f:
        f.write(patched.encode("utf-8"))

    print("%s: patched numpy.hpp with reusable KissFFT plan" % SCRIPT_NAME)
    print("  -> %s" % numpy_path)
    print(
        "  -> NOTE: run a full clean build once after replacing the Edge Impulse SDK "
        "(pio run -e esp32dev-ei -t clean)."
    )
    return True


def run_self_test():
    with tempfile.TemporaryDirectory(prefix="eloc-ei-fft-cache-") as temp_dir:
        for fixture_name, newline in (("numpy-lf.hpp", "\n"), ("numpy-crlf.hpp", "\r\n")):
            fixture_path = os.path.join(temp_dir, fixture_name)
            fixture = (
                "prefix\n" + ORIGINAL_BLOCK + "suffix\n"
            ).replace("\n", newline)
            with open(fixture_path, "wb") as f:
                f.write(fixture.encode("utf-8"))

            if not patch_numpy_file(fixture_path):
                raise PatchError("self-test expected the fresh-export fixture to be patched")

            with open(fixture_path, "rb") as f:
                once_patched = f.read()
            if SENTINEL.encode("utf-8") not in once_patched:
                raise PatchError("self-test patch output is missing the sentinel")

            if patch_numpy_file(fixture_path):
                raise PatchError("self-test expected the second pass to be a no-op")
            with open(fixture_path, "rb") as f:
                twice_patched = f.read()
            if once_patched != twice_patched:
                raise PatchError("self-test idempotence check changed the file")

        incompatible_path = os.path.join(temp_dir, "future-numpy.hpp")
        with open(incompatible_path, "wb") as f:
            f.write(b"future Edge Impulse implementation\n")
        try:
            patch_numpy_file(incompatible_path)
        except PatchError:
            pass
        else:
            raise PatchError("self-test expected an incompatible SDK to fail closed")

    print("%s: self-test passed" % SCRIPT_NAME)


def fail(message, pio_env=None):
    sys.stderr.write("ERROR [%s]: %s\n" % (SCRIPT_NAME, message))
    if pio_env is not None:
        pio_env.Exit(1)
    raise PatchError(message)


def platformio_main(pio_env):
    project_dir = pio_env.subst("$PROJECT_DIR")
    numpy_path = os.path.join(
        project_dir,
        "lib",
        "edge-impulse",
        "src",
        "edge-impulse-sdk",
        "dsp",
        "numpy.hpp",
    )
    try:
        patch_numpy_file(numpy_path)
    except PatchError as exc:
        fail(str(exc), pio_env)


try:
    Import("env")  # Provided by PlatformIO/SCons; capital I is intentional.
    _PIO_ENV = env
except NameError:
    _PIO_ENV = None


if _PIO_ENV is not None:
    platformio_main(_PIO_ENV)
elif __name__ == "__main__":
    try:
        if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
            run_self_test()
        elif len(sys.argv) == 3 and sys.argv[1] == "--target":
            patch_numpy_file(os.path.abspath(sys.argv[2]))
        else:
            sys.stderr.write(
                "Usage: python tools/%s --self-test | --target <numpy.hpp>\n" % SCRIPT_NAME
            )
            sys.exit(2)
    except PatchError as exc:
        fail(str(exc))
