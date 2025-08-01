EESchema Schematic File Version 4
EELAYER 30 0
EELAYER END
$Descr A4 11693 8268
encoding utf-8
Sheet 4 6
Title ""
Date "2025-07-28"
Rev "1.0"
Comp "RetroBit Lab"
Comment1 "Gianluca Renzi"
Comment2 ""
Comment3 ""
Comment4 ""
$EndDescr
$Comp
L RetroBitLab:ECIBUS ECI1
U 1 1 68934ECE
P 4250 2550
F 0 "ECI1" H 4250 3065 50  0000 C CNN
F 1 "ECIBUS" H 4250 2974 50  0000 C CNN
F 2 "" H 4250 1700 50  0001 C CNN
F 3 "" H 4250 1700 50  0000 C CNN
	1    4250 2550
	1    0    0    -1  
$EndComp
$Comp
L RetroBitLab:ATARI-CARTRIDGE CART1
U 1 1 68935683
P 7525 2600
F 0 "CART1" H 7525 3515 50  0000 C CNN
F 1 "ATARI-CARTRIDGE" H 7525 3424 50  0000 C CNN
F 2 "" H 7525 1750 50  0001 C CNN
F 3 "" H 7525 1750 50  0000 C CNN
	1    7525 2600
	1    0    0    -1  
$EndComp
Text GLabel 3275 2300 0    50   Output ~ 0
~EXSEL
Text GLabel 3000 2400 0    50   Output ~ 0
~RST
Text GLabel 3025 2600 0    50   Output ~ 0
~MPD
Text GLabel 3300 2700 0    50   Output ~ 0
AUDIO
Text GLabel 3300 2900 0    50   Output ~ 0
5V
Wire Wire Line
	3300 2900 3350 2900
$Comp
L power:+5V #PWR068
U 1 1 68937678
P 3250 3025
F 0 "#PWR068" H 3250 2875 50  0001 C CNN
F 1 "+5V" V 3265 3153 50  0000 L CNN
F 2 "" H 3250 3025 50  0001 C CNN
F 3 "" H 3250 3025 50  0001 C CNN
	1    3250 3025
	0    -1   -1   0   
$EndComp
Wire Wire Line
	3250 3025 3350 3025
Wire Wire Line
	3350 3025 3350 2900
Connection ~ 3350 2900
Wire Wire Line
	3300 2700 3350 2700
Wire Wire Line
	3025 2600 3350 2600
Wire Wire Line
	3300 2500 3350 2500
Wire Wire Line
	3350 2400 3000 2400
Wire Wire Line
	3275 2300 3350 2300
Text GLabel 5500 2400 2    50   Output ~ 0
~IRQ
Text GLabel 5500 2600 2    50   Output ~ 0
A13
Text GLabel 5500 2700 2    50   Output ~ 0
A14
Text GLabel 5500 2800 2    50   Output ~ 0
A15
Text GLabel 5200 2900 2    50   Output ~ 0
GND
$Comp
L power:GND #PWR069
U 1 1 6893A321
P 5300 3025
F 0 "#PWR069" H 5300 2775 50  0001 C CNN
F 1 "GND" H 5305 2852 50  0000 C CNN
F 2 "" H 5300 3025 50  0001 C CNN
F 3 "" H 5300 3025 50  0001 C CNN
	1    5300 3025
	1    0    0    -1  
$EndComp
Wire Wire Line
	5150 2900 5150 3025
Wire Wire Line
	5150 3025 5300 3025
Wire Wire Line
	5150 2900 5200 2900
Connection ~ 5150 2900
Wire Wire Line
	5150 2800 5500 2800
Wire Wire Line
	5150 2700 5500 2700
Wire Wire Line
	5150 2600 5500 2600
Wire Wire Line
	5150 2400 5500 2400
Text GLabel 6625 2350 0    50   Output ~ 0
A0
Text GLabel 6625 2250 0    50   Output ~ 0
A1
Text GLabel 6625 2150 0    50   Output ~ 0
A2
Text GLabel 6625 2050 0    50   Output ~ 0
A3
Text GLabel 8425 2150 2    50   Output ~ 0
A4
Text GLabel 8425 2250 2    50   Output ~ 0
A5
Text GLabel 8425 2350 2    50   Output ~ 0
A6
Text GLabel 8425 2450 2    50   Output ~ 0
A7
Text GLabel 8425 2550 2    50   Output ~ 0
A8
Text GLabel 8425 2650 2    50   Output ~ 0
A9
Text GLabel 8425 3150 2    50   Output ~ 0
A10
Text GLabel 8425 3050 2    50   Output ~ 0
A11
Text GLabel 8425 2750 2    50   Output ~ 0
A12
Text GLabel 6625 2850 0    50   Output ~ 0
D0
Text GLabel 6625 2750 0    50   Output ~ 0
D1
Text GLabel 6625 2650 0    50   Output ~ 0
D2
Text GLabel 8425 2850 2    50   Output ~ 0
D3
Text GLabel 6625 2450 0    50   Output ~ 0
D4
Text GLabel 6625 2550 0    50   Output ~ 0
D5
Text GLabel 6625 2950 0    50   Output ~ 0
D6
Text GLabel 8425 2950 2    50   Output ~ 0
D7
Text GLabel 8425 2050 2    50   Output ~ 0
GND
Text GLabel 6625 3150 0    50   Output ~ 0
5V
Text GLabel 8650 3250 2    50   Output ~ 0
R~W
Text GLabel 8425 3350 2    50   Output ~ 0
PHI2
Wire Wire Line
	8425 3250 8650 3250
NoConn ~ 3300 2500
Text Notes 3150 1500 0    197  ~ 39
ATARI 130XE ECI & CARTRIDGE INTERFACE
Wire Wire Line
	3300 2800 3350 2800
NoConn ~ 3300 2800
NoConn ~ 5250 2500
Wire Wire Line
	5150 2500 5250 2500
NoConn ~ 6575 3050
NoConn ~ 6575 1950
Wire Wire Line
	8475 1950 8425 1950
NoConn ~ 8475 1950
Wire Wire Line
	6575 3250 6625 3250
NoConn ~ 6575 3250
Wire Wire Line
	6575 3050 6625 3050
Wire Wire Line
	6575 1950 6625 1950
NoConn ~ 5250 2300
Wire Wire Line
	5150 2300 5250 2300
Text GLabel 6625 3350 0    39   BiDi ~ 0
~CCTL
Text GLabel 3650 4000 0    39   Output ~ 0
AUDIOL
Text GLabel 3650 4100 0    39   Output ~ 0
AUDIOR
Wire Wire Line
	3650 4000 3975 4000
Wire Wire Line
	3650 4100 3975 4100
Wire Wire Line
	4175 4000 4400 4000
Wire Wire Line
	4400 4000 4400 4050
Wire Wire Line
	4400 4100 4175 4100
Wire Wire Line
	4400 4050 4525 4050
Connection ~ 4400 4050
Wire Wire Line
	4400 4050 4400 4100
Text GLabel 4525 4050 2    50   Output ~ 0
AUDIO
Text Notes 3000 3825 0    118  Italic 24
AUDIO MIXER INSIDE ATARI
Wire Notes Line
	5525 3575 5525 4350
Wire Notes Line
	5525 4350 2900 4350
Wire Notes Line
	2900 4350 2900 3575
Wire Notes Line
	2900 3575 5525 3575
$Comp
L Device:R_Small R?
U 1 1 6896DB43
P 4075 4000
AR Path="/68821B60/6896DB43" Ref="R?"  Part="1" 
AR Path="/68821C2E/6896DB43" Ref="R35"  Part="1" 
F 0 "R35" V 4025 3825 50  0000 C CNN
F 1 "2K7" V 4000 4000 50  0000 C CNN
F 2 "Resistor_SMD:R_1206_3216Metric_Pad1.30x1.75mm_HandSolder" H 4075 4000 50  0001 C CNN
F 3 "~" H 4075 4000 50  0001 C CNN
F 4 "C137330" H 4075 4000 50  0001 C CNN "LCSC"
	1    4075 4000
	0    1    1    0   
$EndComp
$Comp
L Device:R_Small R?
U 1 1 6896E122
P 4075 4100
AR Path="/68821B60/6896E122" Ref="R?"  Part="1" 
AR Path="/68821C2E/6896E122" Ref="R36"  Part="1" 
F 0 "R36" V 4125 3925 50  0000 C CNN
F 1 "2K7" V 4150 4100 50  0000 C CNN
F 2 "Resistor_SMD:R_1206_3216Metric_Pad1.30x1.75mm_HandSolder" H 4075 4100 50  0001 C CNN
F 3 "~" H 4075 4100 50  0001 C CNN
F 4 "C137330" H 4075 4100 50  0001 C CNN "LCSC"
	1    4075 4100
	0    1    1    0   
$EndComp
$EndSCHEMATC
