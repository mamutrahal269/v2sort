package utils

import (
	"fmt"
	"math"
	"strconv"
	"strings"
)

type unitDef struct {
	names          []string
	bytesPerSecond float64
}

// unitDefs содержит только базовые имена единиц. Формы с суффиксом "/s"
// (например, "mb/s", "kibibyte/s") отдельно не перечисляются: ParseSpeed
// отрезает суффикс перед поиском в unitMultipliers.
var unitDefs = []unitDef{
	// Бит/с
	{[]string{"bps", "bit", "bits"}, 1.0 / 8},
	{[]string{"kbps", "kbit", "kbits"}, 1e3 / 8},
	{[]string{"kibps", "kibit", "kibits"}, 1024.0 / 8},
	{[]string{"mbps", "mbit", "mbits"}, 1e6 / 8},
	{[]string{"mibps", "mibit", "mibits"}, 1024.0 * 1024 / 8},
	{[]string{"gbps", "gbit", "gbits"}, 1e9 / 8},
	{[]string{"gibps", "gibit", "gibits"}, 1024.0 * 1024 * 1024 / 8},

	// Байт/с
	{[]string{"b", "byte", "bytes"}, 1},
	{[]string{"kb", "kbyte", "kbytes"}, 1e3},
	{[]string{"kib", "kibibyte", "kibibytes"}, 1024},
	{[]string{"mb", "mbyte", "mbytes"}, 1e6},
	{[]string{"mib", "mibibyte", "mibibytes"}, 1024.0 * 1024},
	{[]string{"gb", "gbyte", "gbytes"}, 1e9},
	{[]string{"gib", "gibibyte", "gibibytes"}, 1024.0 * 1024 * 1024},
}

var unitMultipliers = func() map[string]float64 {
	m := make(map[string]float64)
	for _, u := range unitDefs {
		for _, name := range u.names {
			m[name] = u.bytesPerSecond
		}
	}
	m[""] = 1e6 / 8
	return m
}()

func ParseSpeed(s string) (uint64, error) {
	s = strings.ToLower(strings.Join(strings.Fields(s), ""))

	i := len(s) - len(strings.TrimLeft(s, "0123456789."))
	value, err := strconv.ParseFloat(s[:i], 64)
	if err != nil {
		return 0, fmt.Errorf("некорректное значение скорости %q", s)
	}

	multiplier, ok := unitMultipliers[strings.TrimSuffix(s[i:], "/s")]
	if !ok {
		return 0, fmt.Errorf("неизвестная единица измерения %q", s[i:])
	}

	bytesPerSecond := value * multiplier
	if bytesPerSecond >= float64(math.MaxUint64) {
		return 0, fmt.Errorf("слишком большое значение скорости %q", s)
	}
	return uint64(bytesPerSecond), nil
}
