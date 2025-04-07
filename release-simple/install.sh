#!/bin/bash

# Check if the script is run with root privileges
if [ "$EUID" -ne 0 ]; then
  echo "Please run this script with root privileges"
  exit 1
fi

TEMP_DIR=$(mktemp -d)
ACTION="$1"

cleanup() {
    rm -rf "$TEMP_DIR"
}

trap cleanup EXIT

case "$ACTION" in
    install)
        # Extract embedded tar.gz file from the script itself
        echo "Extracting embedded files..."
        tail -n +$(awk '/^__TARFILE__/ {print NR+1; exit 0;}' "$0") "$0" | tar -xz -C "$TEMP_DIR" || { echo "Failed to extract files"; exit 1; }

        # Install required dependencies (gcc-c++ and tar)
        echo "Installing required dependencies: gcc-c++ and tar..."
        dnf install -y gcc-c++ tar || { echo "Failed to install dependencies"; }
	apt-get install g++ tar || { echo "Failed to install dependencies"; }

        # Enter the extracted project directory
        cd "$TEMP_DIR/FanControl" || { echo "Failed to enter project directory"; exit 1; }

        # Compile source code
        echo "Compiling source code..."
        make || { echo "Failed to compile source code"; exit 1; }
        make install || { echo "Failed to install binaries"; exit 1; }

        # Set up the service to start on boot
        echo "Setting up the service to start on boot..."
        chmod +x /usr/local/bin/AutoFanCtrl /usr/local/bin/ManFanCtrl
        systemctl daemon-reload
        systemctl enable autofanctrl.service || { echo "Failed to enable service"; exit 1; }
        systemctl start autofanctrl.service || { echo "Failed to start service"; exit 1; }

        echo "Installation completed!"
        ;;
    uninstall)
        # Uninstall the service and remove related files
        echo "Uninstalling the service..."
        if systemctl is-active --quiet autofanctrl.service; then
            systemctl stop autofanctrl.service
        fi
        systemctl disable autofanctrl.service || true
        sudo rm -f /lib/systemd/system/autofanctrl.service || true
        systemctl daemon-reload

        echo "Removing installed files..."
        sudo rm -f /usr/local/bin/AutoFanCtrl /usr/local/bin/ManFanCtrl || true

        echo "Uninstallation completed!"
        ;;
    *)
        echo "Usage: $0 {install|uninstall}"
        exit 1
        ;;
esac

exit 0

__TARFILE__
