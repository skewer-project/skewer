#!/bin/bash
# Regenerate Go Protobufs for Skewer Coordinator

PROTO_DIR="api/proto"
V1_DIR="$PROTO_DIR/coordinator/v1"

echo "Regenerating Go Protobufs in $V1_DIR..."

protoc --go_out=. --go_opt=paths=source_relative        --go-grpc_out=. --go-grpc_opt=paths=source_relative        -I .        "$V1_DIR/coordinator.proto"

echo "Done."
