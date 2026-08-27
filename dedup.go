package main

import (
	"encoding/json"
	"github.com/xtls/xray-core/infra/conf"
)

func dedup(outbounds []conf.OutboundDetourConfig) []conf.OutboundDetourConfig {
	seen := map[string]bool{}
	var result []conf.OutboundDetourConfig

	for _, ob := range outbounds {
		clone := ob
		clone.Tag = ""
		data, _ := json.Marshal(&clone)
		key := string(data)
		if !seen[key] {
			seen[key] = true
			result = append(result, ob)
		}
	}
	return result
}
