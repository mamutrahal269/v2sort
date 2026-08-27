package proxy

import (
	"context"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"

	"github.com/xtls/xray-core/common/net"
	"github.com/xtls/xray-core/common/net/cnc"
	"github.com/xtls/xray-core/common/session"
	"github.com/xtls/xray-core/core"
	"github.com/xtls/xray-core/features/outbound"
	"github.com/xtls/xray-core/transport"
	"github.com/xtls/xray-core/transport/pipe"
)

type ConnMetrics struct {
	Speed     int
	Code      int
	TotalTime time.Duration
}

type Geo struct {
	IPv4    string
	IPv6    string
	Country string
}

func request(instance *core.Instance, outboundTag string, rawUrl string, timeout time.Duration) (*http.Response, error) {
	u, err := url.Parse(rawUrl)
	if err != nil {
		return nil, err
	}

	hostname := u.Hostname()
	port := u.Port()
	if port == "" {
		if u.Scheme == "https" {
			port = "443"
		} else {
			port = "80"
		}
	}
	portInt, _ := strconv.Atoi(port)

	dest := net.Destination{
		Address: net.ParseAddress(hostname),
		Port:    net.Port(portInt),
		Network: net.Network_TCP,
	}

	client := &http.Client{
		Timeout: timeout,
		Transport: &http.Transport{
			DisableKeepAlives: true,
			DialContext: func(ctx context.Context, network, addr string) (net.Conn, error) {
				om := instance.GetFeature(outbound.ManagerType()).(outbound.Manager)
				handler := om.GetHandler(outboundTag)
				if handler == nil {
					return nil, fmt.Errorf("unknown outbound tag: %s", outboundTag)
				}

				uplinkReader, uplinkWriter := pipe.New()
				downlinkReader, downlinkWriter := pipe.New()

				link := &transport.Link{Reader: uplinkReader, Writer: downlinkWriter}
				ctx = session.ContextWithOutbounds(ctx, []*session.Outbound{{Target: dest}})

				go handler.Dispatch(ctx, link)

				return cnc.NewConnection(
					cnc.ConnectionInputMulti(uplinkWriter),
					cnc.ConnectionOutputMulti(downlinkReader),
				), nil
			},
		},
	}
	return client.Get(rawUrl)
}

func UrlTest(instance *core.Instance, outboundTag string, targetURL string, timeout time.Duration) (ConnMetrics, error) {
	start := time.Now()

	resp, err := request(instance, outboundTag, targetURL, timeout)
	if err != nil {
		return ConnMetrics{}, err
	}
	defer resp.Body.Close()

	elapsed := time.Since(start)

	body, _ := io.ReadAll(resp.Body)
	size := len(body)

	speed := 0
	if elapsed.Seconds() > 0 {
		speed = int(float64(size) / elapsed.Seconds())
	}

	return ConnMetrics{
		Speed:     speed,
		Code:      resp.StatusCode,
		TotalTime: elapsed,
	}, nil
}

func GetGeo(instance *core.Instance, outboundTag string, timeout time.Duration) (Geo, error) {
	var geo Geo

	getValue := func(body []byte, key string) string {
		pattern := key + "="
		start := strings.Index(string(body), pattern)
		if start == -1 {
			return ""
		}
		start += len(pattern)
		end := strings.IndexByte(string(body[start:]), '\n')
		if end == -1 {
			return string(body[start:])
		}
		return string(body[start : start+end])
	}

	tests := []struct {
		url string
		set func(body []byte)
	}{
		{"https://1.1.1.1/cdn-cgi/trace", func(body []byte) {
			geo.IPv4 = getValue(body, "ip")
			geo.Country = getValue(body, "loc")
		}},
		{"https://[2606:4700:4700::1111]/cdn-cgi/trace", func(body []byte) {
			geo.IPv6 = getValue(body, "ip")
		}},
	}

	for _, t := range tests {
		resp, err := request(instance, outboundTag, t.url, timeout)
		if err != nil {
			continue
		}
		body, _ := io.ReadAll(resp.Body)
		resp.Body.Close()
		t.set(body)
	}

	if geo.IPv4 == "" && geo.IPv6 == "" {
		return Geo{}, fmt.Errorf("both ipv4 and ipv6 requests failed")
	}

	return geo, nil
}
