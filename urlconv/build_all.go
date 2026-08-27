package urlconv

import (
	"errors"
	"fmt"
	"strconv"
	"strings"
	"sync"

	"github.com/xtls/xray-core/infra/conf"

	"v2sort/utils"
)

func BuildAll(list []string, jobs int) (
	outbounds []conf.OutboundDetourConfig,
	tagUrl map[string]string,
	errs []utils.Pair[string, error],
) {
	type result struct {
		index    int
		outbound conf.OutboundDetourConfig
		url      string
		err      error
	}
	tagUrl = make(map[string]string)
	ch := make(chan result, len(list))
	var wg sync.WaitGroup
	sem := make(chan struct{}, jobs)

	process := func(idx int, rawUrl string) {
		defer wg.Done()
		sem <- struct{}{}
		defer func() { <-sem }()

		u := strings.TrimSpace(rawUrl)
		scheme, _, found := strings.Cut(u, "://")
		if !found {
			ch <- result{idx, conf.OutboundDetourConfig{}, rawUrl, errors.New("not a url")}
			return
		}

		tag := strconv.Itoa(idx)
		var ob *conf.OutboundDetourConfig
		var err error

		switch scheme {
		case "vless":
			ob, err = Vless(u, tag)
		case "vmess":
			ob, err = Vmess(u, tag)
		case "ss":
			ob, err = Shadowsocks(u, tag)
		case "trojan":
			ob, err = Trojan(u, tag)
		case "http", "https":
			ob, err = Http(u, tag)
		case "socks5":
			ob, err = Socks5(u, tag)
		case "hysteria2", "hy2":
			ob, err = Hysteria2(u, tag)
		default:
			ch <- result{idx, conf.OutboundDetourConfig{}, rawUrl, fmt.Errorf("unsupported scheme: %s", scheme)}
			return
		}

		if err != nil {
			ch <- result{idx, conf.OutboundDetourConfig{}, rawUrl, err}
			return
		}
		ch <- result{idx, *ob, rawUrl, nil}
	}

	for i, u := range list {
		wg.Add(1)
		go process(i, u)
	}

	wg.Wait()
	close(ch)

	results := make([]result, len(list))
	for r := range ch {
		results[r.index] = r
	}

	for i, r := range results {
		if r.err != nil {
			errs = append(errs, utils.Pair[string, error]{First: r.url, Second: r.err})
		} else {
			tag := strconv.Itoa(i)
			outbounds = append(outbounds, r.outbound)
			tagUrl[tag] = r.url
		}
	}

	return
}
