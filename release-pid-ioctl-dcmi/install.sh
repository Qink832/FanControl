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

        # Check if gcc/g++ and tar are already installed
        echo "Check if gcc/g++ and tar are already installed..."
	if ! command -v g++ &>/dev/null || ! command -v tar &>/dev/null; then
	    # Try to install using dnf (RHEL/CentOS/Fedora)
	    echo "Try to install gcc/g++ and tar..."
	    if command -v dnf &>/dev/null; then
		if ! dnf install -y gcc-c++ tar 2>/dev/null; then
		    echo "Failed to install dependencies with dnf!"
		fi
	    fi
	    
	    # If dnf failed or not available, try apt (Debian/Ubuntu)
	    if ! command -v g++ &>/dev/null || ! command -v tar &>/dev/null; then
		if command -v apt-get &>/dev/null; then
		    if ! apt-get install -y g++ tar 2>/dev/null; then
		        echo "Failed to install dependencies with apt!"
		    fi
		fi
	    fi
	    
	    # Final check if installation succeeded
	    if ! command -v g++ &>/dev/null || ! command -v tar &>/dev/null; then
		echo "Failed to install dependencies! Exit."
		exit 1
	    fi
	fi
	 

        # Enter the extracted project directory
        cd "$TEMP_DIR/FanControl" || { echo "Failed to enter project directory"; exit 1; }

        # Compile source code
        echo "Compiling source code..."
        make || { echo "Failed to compile source code"; exit 1; }
        make install || { echo "Failed to install binaries"; exit 1; }

        # Set up the service to start on boot
        echo "Setting up the service to start on boot..."
        chmod +x /usr/local/bin/AutoFanCtrl /usr/local/bin/ManFanCtrl
        if command -v restorecon &> /dev/null; then
	    restorecon -v /etc/systemd/system/autofanctrl.service
	    restorecon -v /lib/systemd/system/autofanctrl.service
	fi
        systemctl daemon-reexec
        systemctl daemon-reload
        systemctl enable autofanctrl.service || { echo "Failed to enable service"; exit 1; }
        systemctl start autofanctrl.service || { echo "Failed to start service"; exit 1; }

        echo "Installation completed!"
        ;;
    uninstall)
        # Uninstall the service and remove related files
        echo "Remove config"
        rm -rf /etc/udev/rules.d/fanctrl.rules
        echo "Uninstalling the service..."
        if systemctl is-active --quiet autofanctrl.service; then
            systemctl stop autofanctrl.service
        fi
        systemctl disable autofanctrl.service || true
        sudo rm -f /lib/systemd/system/autofanctrl.service || true
        systemctl daemon-reload

        echo "Removing installed files..."
        sudo rm -f /usr/local/bin/AutoFanCtrl /usr/local/bin/ManFanCtrl /etc/FanControlParams.json || true

        echo "Uninstallation completed!"
        ;;
    *)
        echo "Usage: $0 {install|uninstall}"
        exit 1
        ;;
esac

exit 0

__TARFILE__
