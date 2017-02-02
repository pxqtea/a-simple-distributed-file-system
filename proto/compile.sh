#!/bin/bash

protoc --go_out=plugins=grpc:. fs.proto
