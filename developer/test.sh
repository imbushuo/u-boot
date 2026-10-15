#!/bin/bash

qemu-system-aarch64 -machine virt,virtualization=on,acpi=on -smp 4 -display gtk -accel tcg -cpu neoverse-n1 -device ramfb -bios u-boot.bin -m 8G \
 -blockdev driver=file,filename=/mnt/f/Scratch/ValidationOS/Boot.vhdx,node-name=hd0 \
 -blockdev driver=vhdx,file=hd0,node-name=vhd_disk \
 -device virtio-blk-pci,drive=vhd_disk \
 -device qemu-xhci,id=xhci \
 -device usb-kbd,bus=xhci.0 \
 -device usb-tablet,bus=xhci.0
