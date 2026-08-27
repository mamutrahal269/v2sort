package main

import (
	"net"
	"strings"
)

func filterByProtocols(urls []string, f filterConf) []string {
	if len(f.List) == 0 {
		return urls
	}
	filtered := urls[:0]
	for _, u := range urls {
		scheme, _, _ := strings.Cut(u, "://")
		ok := false
		for _, p := range f.List {
			if strings.EqualFold(scheme, p) {
				ok = true
				break
			}
		}
		if f.Allow == ok {
			filtered = append(filtered, u)
		}
	}
	return filtered
}

func filterByCountries(results []testResult, f filterConf) []testResult {
	if len(f.List) == 0 {
		return results
	}
	filtered := results[:0]
	for _, r := range results {
		ok := false
		for _, c := range f.List {
			if strings.EqualFold(r.geo.Country, c) {
				ok = true
				break
			}
		}
		if f.Allow == ok {
			filtered = append(filtered, r)
		}
	}
	return filtered
}

func filterByCIDRs(results []testResult, f filterConf) []testResult {
	if len(f.List) == 0 {
		return results
	}
	networks := make([]*net.IPNet, len(f.List))
	for i, c := range f.List {
		_, networks[i], _ = net.ParseCIDR(c)
	}

	filtered := results[:0]
	for _, r := range results {
		ip := net.ParseIP(r.geo.IPv4)
		if ip == nil {
			ip = net.ParseIP(r.geo.IPv6)
		}
		ok := false
		if ip != nil {
			for _, n := range networks {
				if n != nil && n.Contains(ip) {
					ok = true
					break
				}
			}
		}
		if f.Allow == ok {
			filtered = append(filtered, r)
		}
	}
	return filtered
}
