#!/bin/bash

# Copyright (C) 2009-2013 The PISM Authors

# Downloads SeaRISE "Present Day Greenland" master dataset NetCDF file, adjusts
# metadata, and saves under new name ready for PISM.  See README.md.

# depends on wget and NCO (ncrename, ncap2, ncatted, ncpdq, ncks; see
# http://nco.sourceforge.net/)

set -e  # exit on error

echo "# =================================================================================="
echo "# PISM std Greenland example: preprocessing"
echo "# =================================================================================="
echo

# get file; see page http://websrv.cs.umt.edu/isis/index.php/Present_Day_Greenland
DATAVERSION=1.1
DATAURL=http://websrv.cs.umt.edu/isis/images/a/a5/
DATANAME=Greenland_5km_v$DATAVERSION.nc

echo "fetching master file ... "
wget -nc ${DATAURL}${DATANAME}   # -nc is "no clobber"
echo "  ... done."
echo

PISMVERSION=pism_$DATANAME
echo -n "creating bootstrapable $PISMVERSION from $DATANAME ... "
# copy the vars we want, and preserve history and global attrs
ncks -O -v mapping,lat,lon,bheatflx,topg,thk,presprcp,smb,airtemp2m $DATANAME $PISMVERSION
# convert from water equiv to ice thickness change rate; assumes ice density 910.0 kg m-3
ncap2 -O -s "precipitation=presprcp*(1000.0/910.0)" $PISMVERSION $PISMVERSION
ncatted -O -a units,precipitation,c,c,"m/year" $PISMVERSION
ncatted -O -a long_name,precipitation,c,c,"ice-equivalent mean annual precipitation rate" $PISMVERSION
# delete incorrect standard_name attribute from bheatflx; there is no known standard_name
ncatted -a standard_name,bheatflx,d,, $PISMVERSION
# use pism-recognized name for 2m air temp
ncrename -O -v airtemp2m,ice_surface_temp $PISMVERSION
ncatted -O -a units,ice_surface_temp,c,c,"Celsius" $PISMVERSION
# use pism-recognized name and standard_name for surface mass balance, after
# converting units
ncap2 -O -s "climatic_mass_balance=(1000.0/910.0)*smb" $PISMVERSION $PISMVERSION
ncatted -O -a standard_name,climatic_mass_balance,m,c,"land_ice_surface_specific_mass_balance" $PISMVERSION
ncatted -O -a units,climatic_mass_balance,m,c,"meters/year" $PISMVERSION
# de-clutter by only keeping vars we want
ncks -O -v mapping,lat,lon,bheatflx,topg,thk,precipitation,ice_surface_temp,climatic_mass_balance \
  $PISMVERSION $PISMVERSION
echo "done."
echo

# extract paleo-climate time series into files suitable for option
# -atmosphere ...,delta_T
TEMPSERIES=pism_dT.nc
echo -n "creating paleo-temperature file $TEMPSERIES from $DATANAME ... "
ncks -O -v oisotopestimes,temp_time_series $DATANAME $TEMPSERIES
ncrename -O -d oisotopestimes,time \
            -v oisotopestimes,time \
            -v temp_time_series,delta_T $TEMPSERIES
# reverse time dimension
ncpdq -O --rdr=-time $TEMPSERIES $TEMPSERIES
# make times follow same convention as PISM
ncap2 -O -s "time=-time" $TEMPSERIES $TEMPSERIES
ncatted -O -a units,time,m,c,"years since 1-1-1" $TEMPSERIES
ncatted -O -a calendar,time,c,c,"365_day" $TEMPSERIES
ncatted -O -a units,delta_T,m,c,"Kelvin" $TEMPSERIES
echo "done."
echo

# extract paleo-climate time series into files suitable for option
# -ocean ...,delta_SL
SLSERIES=pism_dSL.nc
echo -n "creating paleo-sea-level file $SLSERIES from $DATANAME ... "
ncks -O -v sealeveltimes,sealevel_time_series $DATANAME $SLSERIES
ncrename -O -d sealeveltimes,time \
            -v sealeveltimes,time \
            -v sealevel_time_series,delta_SL $SLSERIES
# reverse time dimension
ncpdq -O --rdr=-time $SLSERIES $SLSERIES
# make times follow same convention as PISM
ncap2 -O -s "time=-time" $SLSERIES $SLSERIES
ncatted -O -a units,time,m,c,"years since 1-1-1" $SLSERIES
ncatted -O -a calendar,time,c,c,"365_day" $SLSERIES
echo "done."
echo

