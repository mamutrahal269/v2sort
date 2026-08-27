package utils

import "encoding/base64"

type Pair[A, B any] struct {
	First  A
	Second B
}

func TryBase64(s string) []byte {
	for _, enc := range []*base64.Encoding{
		base64.StdEncoding,
		base64.URLEncoding,
		base64.RawStdEncoding,
		base64.RawURLEncoding,
	} {
		if d, err := enc.DecodeString(s); err == nil {
			return d
		}
	}
	return nil
}
