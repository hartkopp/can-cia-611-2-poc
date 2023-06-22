# can-cia-611-2-poc
PoC for CAN CiA 611-2 Multi-PDU

* composing of multiple C-PDUs into M-PDUs
* decomposing of M-PDUs into multiple C-PDUs
* "send trigger" for composed M-PDUs
  * M-PDU buffer full
  * timeout (since first C-PDU of the M-PDU)
  * on demand mode is not implemented
* M-PDU SDT 0x08 (currently in discussion)

### Files

* sdt2mpdu : compose multiple C-PDUs into M-PDUs
* mpdu2sdt : decompose M-PDUs into multiple C-PDUs

#### Not used in below PoC

* canxlgen : generate CAN XL traffic (optional: with test data)
* canxlrcv : display CAN XL traffic (optional: check test data)

#### To generate SDT 0x06 and SDT 0x07 traffic from https://github.com/hartkopp/can-cia-611-1-poc

* ccfd2xl : convert CC/FD to CAN XL (switchable SDT 0x03 or 0x06/0x07)
* xl2ccfd : convert CAN XL to CC/FD (supports SDT 0x03/0x06/0x07)

#### To generate and display Classical CAN and CAN FD traffic from https://github.com/linux-can/can-utils

* cangen : generate CC/FD traffic
* candump : display CC/FD traffic

#### To create and remove virtual CAN interfaces

* create_canxl_vcans.sh : script to create virtual CAN XL interfaces

### PoC test setup and data flow

`cangen` -> vcan0 -> `ccfd2xl` -> xlsrc -> `sdt2mpdu` -> xlmpdu -> `mpdu2sdt` -> xldst -> `xl2ccfd` -> vcan1 -> `candump`

The output of `candump vcan0` and `candump vcan1` can be compared after unifying the CAN interface names to prove the identical content.

### Build the tools

* Just type 'make' to build the tools.
* 'make install' would install the tools in /usr/local/bin (optional)

* build `ccfd2xl` and `xl2ccfd` from https://github.com/hartkopp/can-cia-611-1-poc to generate SDT 0x06 and SDT 0x07 test data traffic

* build and install can-utils from https://github.com/linux-can/can-utils

### Run the PoC

* create virtual CAN XL interfaces with 'create_canxl_vcans.sh start' (as root)
  * vcan0 : virtual CAN bus for Classical CAN and CAN FD traffic generation
  * xlsrc : virtual CAN bus with original (source) CAN XL SDT traffic
  * xlmpdu : virtual CAN bus with composed M-PDU CAN XL traffic
  * xldst : virtual CAN bus with decomposed CAN XL SDT traffic
  * vcan1 : virtual CAN bus for decomposed Classical CAN and CAN FD traffic

* open 6 terminals to run different tools
  1. candump vcan1
  2. ./ccfd2xl vcan0 xlsrc -v
  3. ./sdt2mpdu -v xlsrc xlmpdu -t 222
  4. ./mpdu2sdt -v xlmpdu xldst -t 222
  5. ./xl2ccfd xldst vcan1 -v
  6. cangen vcan0 -I i -n 5000 -g2 -m
