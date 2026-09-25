package main

import (
	"bytes"
	"strings"
	"testing"
)

func TestParseArgs(t *testing.T) {
	tests := []struct {
		name       string
		args       []string
		wantCode   int
		wantDone   bool
		wantStdout string
		wantStderr string
		wantDebug  bool
		wantInput  string
	}{
		{name: "no args opens the window", args: nil},
		{name: "debug", args: []string{"-debug"}, wantDebug: true},
		{name: "short help", args: []string{"-h"}, wantDone: true, wantStdout: "usage: gessetto"},
		{name: "long help", args: []string{"--help"}, wantDone: true, wantStdout: "usage: gessetto"},
		{name: "version", args: []string{"-version"}, wantDone: true, wantStdout: Version},
		{name: "unknown flag", args: []string{"-nope"}, wantCode: 2, wantDone: true, wantStderr: "not defined"},
		{name: "window opens an image", args: []string{"x.png"}, wantInput: "x.png"},
		{name: "window takes one image", args: []string{"x.png", "y.png"}, wantCode: 2, wantDone: true, wantStderr: `unexpected argument "y.png"`},
		{name: "apply with input", args: []string{"-apply", "a.filo", "-out", "o.png", "in.png"}, wantInput: "in.png"},
		{name: "apply without input", args: []string{"-apply", "-", "-out", "-"}},
		{name: "apply needs out", args: []string{"-apply", "a.filo"}, wantCode: 2, wantDone: true, wantStderr: "-apply needs -out"},
		{name: "out alone", args: []string{"-out", "o.png"}, wantCode: 2, wantDone: true, wantStderr: "only used with -apply"},
		{name: "apply takes one input", args: []string{"-apply", "a.filo", "-out", "o.png", "a.png", "b.png"}, wantCode: 2, wantDone: true, wantStderr: `unexpected argument "b.png"`},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			var stdout, stderr bytes.Buffer
			opts, code, done := parseArgs(tt.args, &stdout, &stderr)
			if code != tt.wantCode || done != tt.wantDone {
				t.Fatalf("code=%d done=%v, want code=%d done=%v", code, done, tt.wantCode, tt.wantDone)
			}
			if opts.input != tt.wantInput {
				t.Errorf("input=%q, want %q", opts.input, tt.wantInput)
			}
			if opts.debug != tt.wantDebug {
				t.Errorf("debug=%v, want %v", opts.debug, tt.wantDebug)
			}
			if !strings.Contains(stdout.String(), tt.wantStdout) {
				t.Errorf("stdout %q, want it to contain %q", stdout.String(), tt.wantStdout)
			}
			if tt.wantStdout == "" && stdout.Len() > 0 {
				t.Errorf("stdout %q, want empty", stdout.String())
			}
			if !strings.Contains(stderr.String(), tt.wantStderr) {
				t.Errorf("stderr %q, want it to contain %q", stderr.String(), tt.wantStderr)
			}
			if tt.wantStderr == "" && stderr.Len() > 0 {
				t.Errorf("stderr %q, want empty", stderr.String())
			}
		})
	}
}
