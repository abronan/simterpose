#!/bin/bash

sudo LD_LIBRARY_PATH=/opt/simgrid/lib/ ./simterpose plat.xml deploy_10000_40.xml --log=sd.:info --log=root.fmt:"'%l: [%c/%p]: %m%n'"
