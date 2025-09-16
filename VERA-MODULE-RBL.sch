EESchema Schematic File Version 4
EELAYER 30 0
EELAYER END
$Descr A4 11693 8268
encoding utf-8
Sheet 1 6
Title "VERA FPGA Audio & Video Board"
Date "2025-09-16"
Rev "1.0"
Comp "RetroBit Lab"
Comment1 "Gianluca Renzi"
Comment2 ""
Comment3 ""
Comment4 ""
$EndDescr
Text Notes 3025 2350 0    197  ~ 39
VERA FPGA
$Sheet
S 2300 1375 3075 1625
U 68821B60
F0 "VeraModule" 79
F1 "vera-fpga.sch" 79
$EndSheet
$Sheet
S 6325 1375 3200 1675
U 68821BC9
F0 "BusDecoder" 79
F1 "busdecoder.sch" 79
$EndSheet
Text Notes 6850 2300 0    197  ~ 39
BUS DECODER
$Sheet
S 4650 3975 3250 1800
U 68821C2E
F0 "CartridgeInterface" 79
F1 "cartridgeInterface.sch" 79
$EndSheet
Text Notes 4775 4950 0    177  ~ 35
CARTRIDGE INTERFACE
$Sheet
S 1475 4000 2900 1775
U 68CF41D8
F0 "Vera FPGA flash" 79
F1 "vera-fpga-flash.sch" 79
$EndSheet
Text Notes 1600 5175 0    177  ~ 35
VERA SPI FLASH\nSD CARD INTERFACE
$Sheet
S 8225 4050 2825 1700
U 688FC286
F0 "PowerSupply" 79
F1 "powersupply.sch" 79
$EndSheet
Text Notes 8575 4950 0    197  ~ 39
POWER SUPPLY
$Comp
L Mechanical:Fiducial FID3
U 1 1 68BFDCEA
P 1460 6860
F 0 "FID3" H 1545 6906 50  0000 L CNN
F 1 "Fiducial" H 1545 6815 50  0000 L CNN
F 2 "Fiducial:Fiducial_0.5mm_Mask1mm" H 1460 6860 50  0001 C CNN
F 3 "~" H 1460 6860 50  0001 C CNN
	1    1460 6860
	1    0    0    -1  
$EndComp
$Comp
L Mechanical:Fiducial FID4
U 1 1 68BFDF14
P 1940 6860
F 0 "FID4" H 2025 6906 50  0000 L CNN
F 1 "Fiducial" H 2025 6815 50  0000 L CNN
F 2 "Fiducial:Fiducial_0.5mm_Mask1mm" H 1940 6860 50  0001 C CNN
F 3 "~" H 1940 6860 50  0001 C CNN
	1    1940 6860
	1    0    0    -1  
$EndComp
$Comp
L Mechanical:Fiducial FID2
U 1 1 68BFE1CD
P 2715 6865
F 0 "FID2" H 2800 6911 50  0000 L CNN
F 1 "Fiducial" H 2800 6820 50  0000 L CNN
F 2 "Fiducial:Fiducial_0.5mm_Mask1mm" H 2715 6865 50  0001 C CNN
F 3 "~" H 2715 6865 50  0001 C CNN
	1    2715 6865
	1    0    0    -1  
$EndComp
$Comp
L Mechanical:Fiducial FID1
U 1 1 68BFE3AF
P 3220 6860
F 0 "FID1" H 3305 6906 50  0000 L CNN
F 1 "Fiducial" H 3305 6815 50  0000 L CNN
F 2 "Fiducial:Fiducial_0.5mm_Mask1mm" H 3220 6860 50  0001 C CNN
F 3 "~" H 3220 6860 50  0001 C CNN
	1    3220 6860
	1    0    0    -1  
$EndComp
Wire Notes Line
	1240 6550 1240 7130
Wire Notes Line
	1240 7130 2380 7130
Wire Notes Line
	2380 7130 2380 6480
Wire Notes Line
	2380 6480 1240 6480
Wire Notes Line
	1240 6480 1240 6545
Wire Notes Line
	2570 6485 2570 7135
Wire Notes Line
	2570 7135 3665 7135
Wire Notes Line
	3665 7135 3665 6485
Wire Notes Line
	3665 6485 2570 6485
Text Notes 1335 6610 0    59   ~ 12
FIDUCIAL TOP
Text Notes 2620 6615 0    59   ~ 12
FIDUCIAL BOTTOM
$Comp
L RetroBitLab:LOGO LOGO1
U 1 1 68C1A132
P 3985 6755
F 0 "LOGO1" H 4010 6808 59  0000 L CNN
F 1 "VERA X16 LOGO" H 4010 6703 59  0000 L CNN
F 2 "RetroBitLab:x16logo" H 3985 6755 59  0001 C CNN
F 3 "" H 3985 6755 59  0001 C CNN
	1    3985 6755
	1    0    0    -1  
$EndComp
$Comp
L RetroBitLab:LOGO LOGO2
U 1 1 68C444DF
P 3985 7025
F 0 "LOGO2" H 4010 7078 59  0000 L CNN
F 1 "ATARI READY" H 4010 6973 59  0000 L CNN
F 2 "RetroBitLab:atari_ready_8mm_rev" H 3985 7025 59  0001 C CNN
F 3 "" H 3985 7025 59  0001 C CNN
	1    3985 7025
	1    0    0    -1  
$EndComp
$Comp
L RetroBitLab:LOGO LOGO3
U 1 1 68C44B75
P 4845 6750
F 0 "LOGO3" H 4870 6803 59  0000 L CNN
F 1 "KiCAD DESIGN" H 4870 6698 59  0000 L CNN
F 2 "Symbol:KiCad-Logo2_6mm_SilkScreen" H 4845 6750 59  0001 C CNN
F 3 "" H 4845 6750 59  0001 C CNN
	1    4845 6750
	1    0    0    -1  
$EndComp
$Comp
L RetroBitLab:LOGO LOGO4
U 1 1 68C454BC
P 4850 7025
F 0 "LOGO4" H 4875 7078 59  0000 L CNN
F 1 "ATARI DUO BUS" H 4875 6973 59  0000 L CNN
F 2 "RetroBitLab:DUOCart_20x11_Rev" H 4850 7025 59  0001 C CNN
F 3 "" H 4850 7025 59  0001 C CNN
	1    4850 7025
	1    0    0    -1  
$EndComp
$Comp
L RetroBitLab:LOGO LOGO5
U 1 1 68C454EE
P 5680 6745
F 0 "LOGO5" H 5705 6798 59  0000 L CNN
F 1 "ESP32_BOBOARD" H 5705 6693 59  0000 L CNN
F 2 "RetroBitLab:ESP32_BOBOARD_5V_21.7mmx21.7mm" H 5680 6745 59  0001 C CNN
F 3 "" H 5680 6745 59  0001 C CNN
	1    5680 6745
	1    0    0    -1  
$EndComp
$EndSCHEMATC
