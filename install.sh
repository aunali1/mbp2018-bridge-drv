#!/bin/bash
cp -rf $PWD /usr/src/apple-bce-0.1
dkms install apple-bce/0.1
echo "Kernel module apple-bce/0.1 installed!"
