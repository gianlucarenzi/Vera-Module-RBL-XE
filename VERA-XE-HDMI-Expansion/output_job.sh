#!/bin/bash

PCBNAME=VERA-XE-HDMI-Expansion
PCBNAMEOUT=${PCBNAME}-XE
KICADBOMSCRIPT=${HOME}/.kicad/bom
PDFOUTPUT=${PCBNAME}.pdf
XSLTPROC=$(which xsltproc)
PYTHON3=$(which python3)

ARGS_NUM=$#
# Lanciare con la versione del PCB!!

if [ ${ARGS_NUM} -lt 1 ]; then
	echo "Need the PCB Version!"
	exit 1
fi

# Need KiCAD CLI from a v7+ version installed
KICADCLI=$(which kicad-cli)
if [ "${KICADCLI}" == "" ]; then
	echo "Need to install kicad-cli from a version of KiCAD v7 or greater"
	echo "(on Debian 11 I am using the flatpak option). Put the script"
	echo "into a path available for shell..."
	exit 1
fi

# La versione e' il primo argomento
PCBVER=$1
PCBNAMEOUT=${PCBNAMEOUT}-v${PCBVER}

# La prima volta va generato il file xml!
if [ ! -f ${PCBNAME}.xml ]; then
	echo "Need to run once the BOM Generator from EESCHEMA / Schematic Designer"
	exit 1
fi

# Creiamo la BOM per JLCPCB
${XSLTPROC} -o ${PCBNAME}.csv ${KICADBOMSCRIPT}/bom2grouped_csv_jlcpcb.xsl ${PCBNAME}.xml

echo "Creating JLCPCB BOM"
${PYTHON3} jlcpcb-check-bom.py ${PCBNAME}.csv ${PCBNAMEOUT}-JLCPCB-BOM.csv

# Generiamo la CPL con uno script simile al plugin di PCBNEW!!!!
./run_generate_cpl.sh ${PCBNAME}.kicad_pcb
if [ $? -ne 0 ]; then
	echo "Error on generating CPL files."
	exit 1
fi

echo "Creating JLCPCB CPL"
if [ -f ${PCBNAME}_cpl_top.csv ]; then
	# Verify if the bot is present
	if [ -f ${PCBNAME}_cpl_bot.csv ]; then
		# We are 2 side board
		./clean_virtual.sh ${PCBNAME}_cpl_top.csv ${PCBNAME}_cpl_bot.csv ${PCBNAME}.kicad_pcb
		cat ${PCBNAME}_cpl_top.csv <(tail -n +2 ${PCBNAME}_cpl_bot.csv) > ${PCBNAMEOUT}-JLCPCB-CPL.csv
	else
		# We have only TOP Side components!
		./clean_virtual.sh ${PCBNAME}_cpl_top.csv ${PCBNAME}.kicad_pcb
		cp ${PCBNAME}_cpl_top.csv ${PCBNAMEOUT}-JLCPCB-CPL.csv
	fi
else
	# Verify if the bot is present
	if [ -f ${PCBNAME}_cpl_bot.csv ]; then
		# We have only BOT side components
		./clean_virtual.sh ${PCBNAME}_cpl_bot.csv ${PCBNAME}.kicad_pcb
		cp ${PCBNAME}_cpl_bot.csv ${PCBNAMEOUT}-JLCPCB-CPL.csv
	else
		# There is no top and no bottom!
		echo "No CPL files present error!"
		exit 1
	fi
fi

echo "Creating PDF..."
${KICADCLI} sch export pdf -o ${PDFOUTPUT} --no-background-color ${PCBNAME}.sch 

echo "Creating STEP 3D object..."
${KICADCLI} pcb export step -o images/${PCBNAME}.step --force --grid-origin --no-dnp --subst-models ${PCBNAME}.kicad_pcb

echo "Creating VRML 3D object..."
${KICADCLI} pcb export vrml -o images/${PCBNAME}.wrl --force --no-dnp ${PCBNAME}.kicad_pcb

# Remove the existing
rm ${PCBNAMEOUT}.zip 2>/dev/null
echo "Generating JLCPCB Project"
cd production
7z -tzip a ../${PCBNAMEOUT}.zip *
exit 0
