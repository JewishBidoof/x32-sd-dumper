# Extraction validation

Current working-tree source snapshots were used, including the USB deployment naming and verifier seven-word-header/redaction changes. Parent files, index, and commits were not changed.

Passed: Bash syntax checks for all four shell scripts; Python compilation; two synthetic verifier regression tests (seven header words and default identity redaction); DCP directory/image MD5 checks and single-member validation of the original independently built runner. Its SHA-256 matched f733fce154d350a89eccce274ed9da71005bdc1c537a0175247a88ab467eb2b7. That runner is not copied or published.

Blocked: the existing 13-case Linux smoke suite was invoked but could not start because no Docker daemon was available. The ARM build and standalone source-fetch pipeline were not executed end to end. No hardware, real block devices, or actual capture contents were inspected. Reproduction of the original binary hash by this extracted pipeline is unverified.

To validate on a Docker host:

```sh
./usb-dumper/tests/smoke-test.sh
python3 usb-dumper/tests/test-verifier.py
./usb-dumper/build.sh dumper
python3 tools/dcp_inspect.py usb-dumper/out/x32-sd-dumper.run
```

Build timestamps and upstream source revisions are fixed; the Debian tag and package versions are not. Preserve the exact container image and dependency versions for byte-reproducible rebuilding. Before publishing binary releases, satisfy the source and notice requirements in LICENSES.md.
