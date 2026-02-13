# systemd service and wrapper

This directory contains a systemd service file and a supplementary wrapper.

The wrapper's job is to be able to configure the daemon at runtime, like the
init.d script does: It calculates the UUID based on the MAC of the first network
device and make a default user-friendly name for the renderer based on the hostname.

The systemd service uses basic hardening provided by DynamicUser=yes

## gmediarender.service

This files should be copied to /etc/systemd/system/.

## gmediarender-wrapper

This file should be copied to /usr/libexec/gmediarender/

It reads the configuration and will start gmediarender

## gmediarender.conf

This file should be copied to /etc/gmediarender/ and
hosts the configuration of the renderer. 


