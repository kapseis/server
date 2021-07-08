package main

import (
	"bytes"
	"encoding/binary"
	"errors"
	"io"
	"os"
	"unicode/utf16"
)

func main() {
	bs, err := os.ReadFile("huts.utf8.txt")
	if err != nil {
		panic(err)
	}
	bbuf := bytes.NewBuffer(bs)
	binary.Write(os.Stdout, binary.BigEndian, uint16(0xFEFF))
	var i int
	for {
		r, _, err := bbuf.ReadRune()
		if err != nil {
			if errors.Is(err, io.EOF) {
				break
			}
			panic(err)
		}
		i++
		r1, r2 := utf16.EncodeRune(r)
		if r1 == 0xfffd && r2 == 0xfffd {
			binary.Write(os.Stdout, binary.BigEndian, uint16(r))
		} else {
			binary.Write(os.Stdout, binary.BigEndian, uint16(r1))
			binary.Write(os.Stdout, binary.BigEndian, uint16(r2))
		}
	}
}
