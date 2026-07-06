"""
verify_scanner.py

Verifies that the VirtualScanner Image Capture device module is installed
and discoverable on macOS, using the `twain-wia-sane-scanner` Python
package (https://pypi.org/project/twain-wia-sane-scanner/), which talks to
the Dynamic Web TWAIN Service over its local REST API.

VirtualScanner.app bundles a set of sample images (Resources/SampleImages,
named 1.png ... 11.png). By default the device scans its built-in test.tiff,
but this script can tell it to use one of the bundled sample images instead
by writing a small selection file that the device module reads at scan time.

Prerequisites:
  1. VirtualScanner.app must be installed, e.g. via:
       ./Scripts/build_and_install.sh
  2. The Dynamic Web TWAIN Service must be installed and running:
       https://demo.dynamsoft.com/DWT/DWTResources/dist/DynamsoftServiceSetup.pkg
  3. pip install twain-wia-sane-scanner

Usage:
  python3 verify_scanner.py
  python3 verify_scanner.py --list-images
  python3 verify_scanner.py --image 3.png
  python3 verify_scanner.py --image all
  python3 verify_scanner.py --host http://127.0.0.1:18622 --license YOUR-LICENSE-KEY
"""

import argparse
import os
import sys

try:
    from dynamsoftservice import ScannerController, ScannerServiceError, ScannerType
except ImportError:
    print("The 'twain-wia-sane-scanner' package is not installed.")
    print("Install it with: pip install twain-wia-sane-scanner")
    sys.exit(1)

# Default trial license used for local verification. Replace with your own key from:
# https://www.dynamsoft.com/customer/license/trialLicense/?product=dcv&package=cross-platform
DEFAULT_LICENSE_KEY = (
    "DLS2eyJoYW5kc2hha2VDb2RlIjoiMjAwMDAxLTE2NDk4Mjk3OTI2MzUiLCJvcmdhbml6YXRpb25JRCI6"
    "IjIwMDAwMSIsInNlc3Npb25QYXNzd29yZCI6IndTcGR6Vm05WDJrcEQ5YUoifQ=="
)
DEFAULT_HOST = "http://127.0.0.1:18622"
DEVICE_NAME_HINT = "Virtual Scanner"

# Candidate install locations for VirtualScanner.app, checked in order.
INSTALLED_APP_PATHS = [
    os.path.expanduser("~/Library/Image Capture/Devices/VirtualScanner.app"),
    "/Library/Image Capture/Devices/VirtualScanner.app",
]

# The VirtualScanner device module reads this file at scan time to decide which
# bundled sample image to return. An empty/missing file means "use the default
# test.tiff image".
SELECTION_FILE_PATH = os.path.expanduser(
    "~/Library/Application Support/VirtualScanner/selected_image.txt"
)


def find_installed_app():
    for path in INSTALLED_APP_PATHS:
        if os.path.isdir(path):
            return path
    return None


def list_bundled_images():
    app_path = find_installed_app()
    if not app_path:
        return []

    images_dir = os.path.join(app_path, "Contents", "Resources", "SampleImages")
    if not os.path.isdir(images_dir):
        return []

    def sort_key(name):
        stem = os.path.splitext(name)[0]
        return (0, int(stem)) if stem.isdigit() else (1, stem)

    return sorted(
        (name for name in os.listdir(images_dir) if not name.startswith(".")),
        key=sort_key,
    )


def set_selected_image(image_name):
    """Writes (or clears) the selection file read by VirtualScanner at scan time."""
    os.makedirs(os.path.dirname(SELECTION_FILE_PATH), exist_ok=True)
    with open(SELECTION_FILE_PATH, "w") as f:
        f.write(image_name or "")


def parse_args():
    parser = argparse.ArgumentParser(description="Verify the VirtualScanner ICA device is working.")
    parser.add_argument("--host", default=DEFAULT_HOST, help="Dynamic Web TWAIN Service REST host.")
    parser.add_argument("--license", default=DEFAULT_LICENSE_KEY, help="Dynamsoft license key.")
    parser.add_argument("--output", default="./output", help="Directory to save the scanned image(s) to.")
    parser.add_argument(
        "--image",
        default=None,
        help="Bundled sample image to scan (e.g. '3.png'), or 'all' to scan every bundled "
        "image in turn, or 'default' to use the built-in test.tiff. If omitted, the "
        "device's current/default image is used.",
    )
    parser.add_argument(
        "--list-images",
        action="store_true",
        help="List the sample images bundled with the installed VirtualScanner.app and exit.",
    )
    return parser.parse_args()


def scan_one(controller, host, license_key, output_dir, virtual_scanner):
    job = controller.createJob(
        host,
        {
            "license": license_key,
            "device": virtual_scanner["device"],
            "autoRun": False,
            "jobTimeout": 60,
            "scannerFailureTimeout": 30,
            "config": {
                "IfShowUI": False,
                "PixelType": 2,
                "Resolution": 200,
                "IfFeederEnabled": False,
                "IfDuplexEnabled": False,
            },
        },
    )
    job_id = job.get("jobuid", "")
    if not job_id:
        raise RuntimeError(f"Failed to create a scan job. Response: {job}")

    try:
        print(f"Created scan job: {job_id}")
        images = controller.getImageFiles(host, job_id, output_dir, imageType="image/png")

        if not images:
            raise RuntimeError("No pages were scanned. VirtualScanner may not be working correctly.")

        print(f"Scanned {len(images)} page(s) into '{output_dir}':")
        for image in images:
            print(f"  - {image}")
        return images
    finally:
        controller.deleteJob(host, job_id)


def main():
    args = parse_args()

    if args.list_images:
        images = list_bundled_images()
        if not images:
            print("No bundled sample images found. Is VirtualScanner.app installed?")
            sys.exit(1)
        print("Bundled sample images:")
        for image in images:
            print(f"  - {image}")
        return

    controller = ScannerController(timeout=60, raise_errors=True)

    try:
        info = controller.getServerInfo(args.host)
        print(f"Dynamic Web TWAIN Service version: {info}")

        print("Looking for ICA scanners (this includes VirtualScanner)...")
        scanners = controller.getDevices(args.host, ScannerType.ICASCANNER)

        if not scanners:
            print("No ICA scanners were found.")
            print("Checks to try:")
            print("  - Is VirtualScanner.app installed under")
            print("    ~/Library/Image Capture/Devices or /Library/Image Capture/Devices?")
            print("  - Open Image Capture.app: does 'Virtual Scanner EX/AF' show up there?")
            print("  - Is the Dynamic Web TWAIN Service running?")
            sys.exit(1)

        print(f"Found {len(scanners)} ICA scanner(s):")
        for scanner in scanners:
            print(f"  - {scanner.get('name')} (device id: {scanner.get('device')})")

        virtual_scanner = next(
            (s for s in scanners if DEVICE_NAME_HINT.lower() in s.get("name", "").lower()),
            scanners[0],
        )
        print(f"Using scanner: {virtual_scanner.get('name')}")

        if args.image in (None, "default"):
            images_to_scan = [None]
        elif args.image == "all":
            images_to_scan = list_bundled_images()
            if not images_to_scan:
                print("No bundled sample images found. Is VirtualScanner.app installed?")
                sys.exit(1)
        else:
            images_to_scan = [args.image]

        for image_name in images_to_scan:
            set_selected_image(image_name if image_name != "default" else None)
            label = image_name or "default (test.tiff)"
            print(f"--- Scanning '{label}' ---")
            scan_one(controller, args.host, args.license, args.output, virtual_scanner)

        print("Success!")

    except ScannerServiceError as error:
        print(f"Dynamic Web TWAIN Service error: {error}")
        print(f"Details: {error.details}")
        sys.exit(1)
    finally:
        controller.close()


if __name__ == "__main__":
    main()

