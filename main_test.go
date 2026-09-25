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
	}{
		{name: "no args opens the window", args: nil},
		{name: "debug", args: []string{"-debug"}, wantDebug: true},
		{name: "short help", args: []string{"-h"}, wantDone: true, wantStdout: "usage: gessetto"},
		{name: "long help", args: []string{"--help"}, wantDone: true, wantStdout: "usage: gessetto"},
		{name: "version", args: []string{"-version"}, wantDone: true, wantStdout: Version},
		{name: "unknown flag", args: []string{"-nope"}, wantCode: 2, wantDone: true, wantStderr: "not defined"},
		{name: "stray argument", args: []string{"x.png"}, wantCode: 2, wantDone: true, wantStderr: `unexpected argument "x.png"`},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			var stdout, stderr bytes.Buffer
			opts, code, done := parseArgs(tt.args, &stdout, &stderr)
			if code != tt.wantCode || done != tt.wantDone {
				t.Fatalf("code=%d done=%v, want code=%d done=%v", code, done, tt.wantCode, tt.wantDone)
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
