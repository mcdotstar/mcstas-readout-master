#!/usr/bin/env bash
echo "Running integration tests..."
set -e
set -o pipefail
set -u
set -o errtrace
set -o functrace
set -o posix
set -o noclobber

# Allow for silent failure if mccode-antlr is not installed
if ! command -v mcstas-antlr &> /dev/null; then
    echo "mcstas-antlr not found, skipping integration tests."
    exit 100
fi
readout_config=""
if [ -x bin/readout-config ]; then
    readout_config="bin/readout-config"
elif [ -x bin/Debug/readout-config.exe ]; then
    readout_config="bin/Debug/readout-config.exe"
elif [ -x bin/Release/readout-config.exe ]; then
    readout_config="bin/Release/readout-config.exe"
else
    echo "local readout-config not found."
    exit 1
fi
# Add the build directory to PATH so that CMD(readout-config ...) in component
# DEPENDENCY lines is resolved when mcstas-antlr processes the .instr file.
export PATH="${PWD}/bin:${PWD}/bin/Debug:${PWD}/bin/Release:${PATH}"
compdir=$("${readout_config}" --show compdir)
compileflags="-Wl,-rpath,lib -Llib -lreadout -Iinclude"

# Switch to a temporary directory
#tmpdir=$(mktemp -d)
#trap 'rm -rf -- "${tmpdir}"' EXIT
#cd ${tmpdir} || exit 1
# Create a temporary file for the test
temp_file="test_instrument.instr"
# Clean up any old generated file
rm -f "${temp_file}"
# And give it some content
/bin/cat <<EOF > "$temp_file"
DEFINE INSTRUMENT test_instrument(int dummy=0, scale/"m"=1, int point=0, int total_points=0, string filename="output")
USERVARS
%{
int RING;
int FEN;
int TUBE;
int A;
int B;
double tof;
%}

TRACE
SEARCH "${compdir}"
COMPONENT origin = Arm() AT (0, 0, 0) ABSOLUTE
EXTEND
%{
RING = 0;
FEN = 0;
TUBE = 0;
A = 0;
B = 0;
tof = 0.0;
x = 0;
y = 0;
z = 0;
vx = 0;
vy = 0;
vz = 1;
p = 1;
%}

COMPONENT readout = ReadoutCAEN(
  ring="RING", fen="FEN", tube="TUBE", event_mode="p", a_name="A", b_name="B", tof="tof", ip="127.0.0.1", port=9000,
  broadcast=0
  )
  AT (0, 0, 1) ABSOLUTE

COMPONENT monitor_readout = ReadoutTTLMonitor(
  ring="RING", fen="FEN", position="A", identity="TUBE", value="B", tof="tof", ip="127.0.0.1", port=9001, broadcast=0
)
  AT (0, 0, 2) ABSOLUTE

COMPONENT caen_collector = CollectorCAEN(
ring="RING", fen="FEN", tube="TUBE", a_name="A", b_name="B", tof="tof",
filename=filename, verbose=1
) AT (0, 0, 3) ABSOLUTE

COMPONENT discrete_monitor = ReadoutDiscreteCAEN(
  ring="RING", fen="FEN", tube="TUBE", a_name="A", b_name="B", tof="tof", ip="127.0.0.1", port=9002
) AT (0, 0, 4) ABSOLUTE

END
EOF

# Convert the test file into a C file
mcstas-antlr ${temp_file} || exit 1
# Compile the C file and run it
mcrun-antlr ${temp_file} -n 100 dummy=1 || exit 1



exit 0
