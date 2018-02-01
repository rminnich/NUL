#!/bin/bash
set -e
make
  mount /dev/sdb1 /mnt/usb/
  cp sigma0 /mnt//usb/nova64/sigma0 
  gzip -f /mnt/usb/nova64/sigma0
   cp ../../NOVA/build/hypervisor-x86_64.32bit /mnt/usb/boot/hypervisor-x86_64.32bit
  umount /mnt/usb
  bash QEMU
