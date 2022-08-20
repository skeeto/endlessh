#/bin/bash
#
#
#
#
# the idea ok
#Usage <on/off>   --- Button
#Usage <>         --- Switch
function killsmc(){
   var= $(echo "mbpfan: aucun processus trouvé" | sudo killall mbpfan)
   return $var
}
function bornsmc(){
   var= $(echo "mbpfan 2.2.0 starting up" | sudo mbpfan)
   return $var
}
function traduceState(){
   if   [  "$1" == "1"  ] || [ "$1" == "on" ] || [ "$1" == "start" ] ; then
	   return 0
   elif [  "$1" == "0"  ] || [ "$1" == "off" ] || [ "$1" == "stop" ] ; then
	   return 1
   else
	    exit 1
   fi	 
}
function main(){
	if traduceState $1; then
		if bornsmc; then
			echo "mbpfan viens de demarrer!\n"
		else
			echo "mbpfan ne demarre pas...\n"
		fi
	else
		if killsmc; then
			echo "Tout processus mbpfan a été tué !\n"
		else
			echo "Il n'y a pas de mbpfan en vie !\n"
		fi
	fi
}
function Button(){
	if [ $# -eq 0 ]
	then
		echo -e "You Can't Use the Button without args.\n"
		echo -e "Help: <on/off> (1/0 or start/stop its okay too)\n"
		echo -e "Usage:"
		echo -e "\t$0 <state>"
		exit 1
	else
		if [ "$1" == "-s"  ] || [ "$1" ==  "--switch" ]; then
			if [ "$(tail -c 2 $0)" == "0" ]; then
				scriptMagic 1
			elif [ "$(tail -c 2 $0)" == "1" ]; then
				scriptMagic 0
			fi
		else
			main $1
		fi
	fi
}


# ============================
Button $1

#===============================================================
#===============================================================
#===============================================================
#===============================================================
#===============================================================
#==============================================================0
