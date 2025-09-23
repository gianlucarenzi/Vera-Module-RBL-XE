EESchema Schematic File Version 4
EELAYER 30 0
EELAYER END
$Descr A3 16535 11693
encoding utf-8
Sheet 1 6
Title "VERA FPGA Audio & Video Board"
Date "2025-09-23"
Rev "1.0"
Comp "RetroBit Lab"
Comment1 "Gianluca Renzi"
Comment2 ""
Comment3 ""
Comment4 ""
$EndDescr
Text Notes 4405 3385 0    197  ~ 39
VERA FPGA
$Sheet
S 3680 2410 3075 1625
U 68821B60
F0 "VeraModule" 79
F1 "vera-fpga.sch" 79
$EndSheet
$Sheet
S 9610 2450 3200 1675
U 68821BC9
F0 "BusDecoder" 79
F1 "busdecoder.sch" 79
$EndSheet
Text Notes 10135 3375 0    197  ~ 39
BUS DECODER
$Sheet
S 6505 6265 3250 1800
U 68821C2E
F0 "CartridgeInterface" 79
F1 "cartridgeInterface.sch" 79
$EndSheet
Text Notes 6630 7240 0    177  ~ 35
CARTRIDGE INTERFACE
$Sheet
S 1285 6220 2900 1775
U 68CF41D8
F0 "Vera FPGA flash" 79
F1 "vera-fpga-flash.sch" 79
$EndSheet
Text Notes 1410 7395 0    177  ~ 35
VERA SPI FLASH\nSD CARD INTERFACE
$Sheet
S 11855 6285 2825 1700
U 688FC286
F0 "PowerSupply" 79
F1 "powersupply.sch" 79
$EndSheet
Text Notes 12205 7185 0    197  ~ 39
POWER SUPPLY
$Comp
L Mechanical:Fiducial FID3
U 1 1 68BFDCEA
P 3405 9825
F 0 "FID3" H 3490 9871 50  0000 L CNN
F 1 "Fiducial" H 3490 9780 50  0000 L CNN
F 2 "Fiducial:Fiducial_0.5mm_Mask1mm" H 3405 9825 50  0001 C CNN
F 3 "~" H 3405 9825 50  0001 C CNN
	1    3405 9825
	1    0    0    -1  
$EndComp
$Comp
L Mechanical:Fiducial FID4
U 1 1 68BFDF14
P 3885 9825
F 0 "FID4" H 3970 9871 50  0000 L CNN
F 1 "Fiducial" H 3970 9780 50  0000 L CNN
F 2 "Fiducial:Fiducial_0.5mm_Mask1mm" H 3885 9825 50  0001 C CNN
F 3 "~" H 3885 9825 50  0001 C CNN
	1    3885 9825
	1    0    0    -1  
$EndComp
$Comp
L Mechanical:Fiducial FID2
U 1 1 68BFE1CD
P 4660 9830
F 0 "FID2" H 4745 9876 50  0000 L CNN
F 1 "Fiducial" H 4745 9785 50  0000 L CNN
F 2 "Fiducial:Fiducial_0.5mm_Mask1mm" H 4660 9830 50  0001 C CNN
F 3 "~" H 4660 9830 50  0001 C CNN
	1    4660 9830
	1    0    0    -1  
$EndComp
$Comp
L Mechanical:Fiducial FID1
U 1 1 68BFE3AF
P 5165 9825
F 0 "FID1" H 5250 9871 50  0000 L CNN
F 1 "Fiducial" H 5250 9780 50  0000 L CNN
F 2 "Fiducial:Fiducial_0.5mm_Mask1mm" H 5165 9825 50  0001 C CNN
F 3 "~" H 5165 9825 50  0001 C CNN
	1    5165 9825
	1    0    0    -1  
$EndComp
Wire Notes Line
	3185 9515 3185 10095
Wire Notes Line
	3185 10095 4325 10095
Wire Notes Line
	4325 10095 4325 9445
Wire Notes Line
	4325 9445 3185 9445
Wire Notes Line
	3185 9445 3185 9510
Wire Notes Line
	4515 9450 4515 10100
Wire Notes Line
	4515 10100 5610 10100
Wire Notes Line
	5610 10100 5610 9450
Wire Notes Line
	5610 9450 4515 9450
Text Notes 3280 9575 0    59   ~ 12
FIDUCIAL TOP
Text Notes 4565 9580 0    59   ~ 12
FIDUCIAL BOTTOM
$Comp
L RetroBitLab:LOGO LOGO1
U 1 1 68C1A132
P 5930 9720
F 0 "LOGO1" H 5955 9773 59  0000 L CNN
F 1 "VERA X16 LOGO" H 5955 9668 59  0000 L CNN
F 2 "RetroBitLab:x16logo" H 5930 9720 59  0001 C CNN
F 3 "" H 5930 9720 59  0001 C CNN
	1    5930 9720
	1    0    0    -1  
$EndComp
$Comp
L RetroBitLab:LOGO LOGO2
U 1 1 68C444DF
P 5930 9990
F 0 "LOGO2" H 5955 10043 59  0000 L CNN
F 1 "ATARI READY" H 5955 9938 59  0000 L CNN
F 2 "RetroBitLab:atari_ready_8mm_rev" H 5930 9990 59  0001 C CNN
F 3 "" H 5930 9990 59  0001 C CNN
	1    5930 9990
	1    0    0    -1  
$EndComp
$Comp
L RetroBitLab:LOGO LOGO3
U 1 1 68C44B75
P 6790 9715
F 0 "LOGO3" H 6815 9768 59  0000 L CNN
F 1 "KiCAD DESIGN" H 6815 9663 59  0000 L CNN
F 2 "Symbol:KiCad-Logo2_6mm_SilkScreen" H 6790 9715 59  0001 C CNN
F 3 "" H 6790 9715 59  0001 C CNN
	1    6790 9715
	1    0    0    -1  
$EndComp
$Comp
L RetroBitLab:LOGO LOGO4
U 1 1 68C454BC
P 6795 9990
F 0 "LOGO4" H 6820 10043 59  0000 L CNN
F 1 "ATARI DUO BUS" H 6820 9938 59  0000 L CNN
F 2 "RetroBitLab:DUOCart_20x11_Rev" H 6795 9990 59  0001 C CNN
F 3 "" H 6795 9990 59  0001 C CNN
	1    6795 9990
	1    0    0    -1  
$EndComp
$Comp
L RetroBitLab:LOGO LOGO5
U 1 1 68C454EE
P 7625 9710
F 0 "LOGO5" H 7650 9763 59  0000 L CNN
F 1 "ESP32_BOBOARD" H 7650 9658 59  0000 L CNN
F 2 "RetroBitLab:ESP32_BOBOARD_5V_21.7mmx21.7mm" H 7625 9710 59  0001 C CNN
F 3 "" H 7625 9710 59  0001 C CNN
	1    7625 9710
	1    0    0    -1  
$EndComp
$Comp
L RetroBitLab:Label-20mmx10mm LBL1
U 1 1 68D3F38A
P 8540 9755
F 0 "LBL1" H 8565 9755 50  0000 L CNN
F 1 "Label-20mmx10mm" H 8540 9655 50  0001 C CNN
F 2 "RetroBitLab:Label-20mmx10mm-Silkscreen" H 8540 9755 50  0001 C CNN
F 3 "" H 8540 9755 50  0001 C CNN
	1    8540 9755
	1    0    0    -1  
$EndComp
$EndSCHEMATC
