#!/bin/bash

PCBNAME=VERA-XE-HDMI-Expansion
PCBNAMEOUT=${PCBNAME}-XE
KICADSCRIPT=${HOME}/.kicad/bom
ARGS_NUM=$#
# Lanciare con la versione del PCB!!

if [ ${ARGS_NUM} -lt 1 ]; then
	echo "Need the PCB Version!"
	exit 1
fi

# La versione e' il primo argomento
PCBVER=$1
PCBNAMEOUT=${PCBNAMEOUT}-v${PCBVER}

# Occorre da eeschema generare la BOM per JLCPCB
# Occorre da pcbnew generare la CPL con il plugin

echo "Creating JLCPCB BOM"
python3 jlcpcb-check-bom.py ${PCBNAME}.csv ${PCBNAMEOUT}-JLCPCB-BOM.csv

echo "Creating JLCPCB CPL"
if [ -f ${PCBNAME}_cpl_top.csv ]; then
	# Verify if the bot is present
	if [ -f ${PCBNAME}_cpl_bot.csv ]; then
		# We are 2 side board
		cat ${PCBNAME}_cpl_top.csv <(tail -n +2 ${PCBNAME}_cpl_bot.csv) > ${PCBNAMEOUT}-JLCPCB-CPL.csv
	else
		# We have only TOP Side components!
		cp ${PCBNAME}_cpl_top.csv ${PCBNAMEOUT}-JLCPCB-CPL.csv
	fi
else
	# Verify if the bot is present
	if [ -f ${PCBNAME}_cpl_bot.csv ]; then
		# We have only BOT side components
		cp ${PCBNAME}_cpl_bot.csv ${PCBNAMEOUT}-JLCPCB-CPL.csv
	else
		# There is no top and no bottom!
		echo "No CPL files present error!"
		exit 1
	fi
fi

# Remove the existing
rm ${PCBNAMEOUT}.zip 2>/dev/null
echo "Generating JLCPCB Project"
cd production
7z -tzip a ../${PCBNAMEOUT}.zip *
exit 0
