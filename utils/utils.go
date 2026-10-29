package utils

import (
	"encoding/base64"
	"strings"
)

type Pair[A, B any] struct {
	First  A
	Second B
}

func DecodeBase64(s string) []byte {
	d, err := base64.RawStdEncoding.DecodeString(
		strings.NewReplacer(
			"-", "+",
			"_", "/",
			"=", "",
		).Replace(s),
	)
	if err != nil {
		return nil
	}
	return d
}
