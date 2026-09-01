package main

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"strings"
	"time"

	"github.com/mamutrahal269/v2sort/utils"
)

func fmtFragment(r testResult, template string) string {
	if template == "" {
		return r.url
	}

	avgTime := time.Duration(0)
	for _, m := range r.metrics {
		avgTime += m.TotalTime
	}
	if len(r.metrics) > 0 {
		avgTime /= time.Duration(len(r.metrics))
	}

	replacer := strings.NewReplacer(
		"%url%", r.url,
		"%ipv4%", orNA(r.geo.IPv4),
		"%ipv6%", orNA(r.geo.IPv6),
		"%country%", orNA(r.geo.Country),
		"%speed%", fmt.Sprintf("%d", r.speed),
		"%speed_kib%", fmt.Sprintf("%d", r.speed/1024),
		"%speed_mib%", fmt.Sprintf("%d", r.speed/1024/1024),
		"%avg_time%", avgTime.String(),
	)
	fragment := replacer.Replace(template)
	scheme, part, _ := strings.Cut(r.url, "://")

	if scheme == "vmess" {
		decoded := utils.TryBase64(part)
		if decoded != nil {
			var vmess map[string]any
			if err := json.Unmarshal(decoded, &vmess); err == nil {
				vmess["ps"] = fragment
				data, _ := json.Marshal(vmess)
				return "vmess://" + base64.StdEncoding.EncodeToString(data)
			}
		}
	}

	hashIdx := strings.LastIndex(r.url, "#")
	if hashIdx == -1 {
		return r.url + "#" + fragment
	}
	return r.url[:hashIdx] + "#" + fragment
}

func orNA(s string) string {
	if s == "" {
		return "N/A"
	}
	return s
}
