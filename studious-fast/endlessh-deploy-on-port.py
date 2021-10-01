#!/usr/bin/python3
#coding: utf-8
import argparse
from os import system
parser = argparse.ArgumentParser("Endlessh fast deploy preset tool | v1 - (Studious-Fast)")
#
# Args to set usages modes
parser.add_argument("-p", "--port", type=int,
                    help="Listening port [22].")
#
args = parser.parse_args()
#
# READ PRESETS DATA
script_endlessh_preset1, service_endlessh_preset1 = "", ""
with open("presets/endlessh-call-port_N.service$",'rb') as FileRead_in1_KC:
    service_endlessh_preset1 = FileRead_in1_KC.read().decode()
    FileRead_in1_KC.close()
# 
with open("presets/endlessh-script-port_N.sh$",'rb') as FileRead_in1_KC:
    script_endlessh_preset1 = FileRead_in1_KC.read().decode()
    FileRead_in1_KC.close()
# ~~~
# Get Endlessh port from argparsing
if args.port: # args setted fine.
    endlessh_port = args.port
else: # no args setted... so default set to (port: 22)...
    endlessh_port = 22
#
# ~~~
# Destination path definition
service_endlessh_path = "/etc/systemd/system"
script_endlessh_path = "/opt/ssh/endlessh"
# Destination filename definition with using the port number inside the names
service_endlessh_filename = "endlessh-call-p{port}.service".format(
    port = endlessh_port
)
script_endlessh_filename = "endlessh-script-p{port}.sh".format(
    port = endlessh_port
)
# Inject setting change port to the presets(in-mem)
service_endlessh_preset1 = service_endlessh_preset1.format(
    port_number = endlessh_port
)
script_endlessh_preset1 = script_endlessh_preset1.format(
    port_number = endlessh_port
)
# Write Files on the system
with open("{service_endlessh_path}/{service_endlessh_filename}".format(
        service_endlessh_path = service_endlessh_path,
        service_endlessh_filename = service_endlessh_filename
    ), 'wb') as f_out_re:
    f_out_re.write(service_endlessh_preset1.encode())
    f_out_re.close()
#
with open("{script_endlessh_path}/{script_endlessh_filename}".format(
        script_endlessh_path = script_endlessh_path,
        script_endlessh_filename = script_endlessh_filename
    ), 'wb') as f_out_re:
    f_out_re.write(script_endlessh_preset1.encode())
    f_out_re.close()
# change execution right to use the launch script
system("sudo chmod +x {}".format(script_endlessh_path+"/"+script_endlessh_filename))
# enable & start the service of the endlessh deploy 
system("sudo systemctl enable --now {service} && sudo systemctl status {service}".format(service = service_endlessh_filename))
exit(0)
