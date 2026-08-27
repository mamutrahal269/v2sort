package urlconv

import (
	"encoding/json"
	"fmt"
	stdurl "net/url"
	"slices"
	"strconv"
	"strings"

	"v2sort/utils"

	"github.com/nlnwa/whatwg-url/url"
	xnet "github.com/xtls/xray-core/common/net"
	"github.com/xtls/xray-core/infra/conf"
)

func derefOr[T any](p *T, def T) T {
	if p == nil {
		return def
	}
	return *p
}

func getQuery(query *url.SearchParams, def string, keys ...string) string {
	for _, key := range keys {
		if val := query.Get(key); val != "" {
			return strings.TrimSpace(val)
		}
	}
	return def
}
func ptr[T any](v T) *T { return &v }
func percentDecode(s string) string {
	d, err := stdurl.PathUnescape(s)
	if err != nil {
		return s
	}
	return d
}

func buildStreamSetting(u *url.Url) (*conf.StreamConfig, error) {
	params := u.SearchParams()
	var sc conf.StreamConfig

	sc.Address = ptr(conf.Address{Address: xnet.ParseAddress(u.Hostname())})
	sc.Port = uint16(u.DecodedPort())

	if fm := getQuery(params, "", "fm"); fm != "" {
		var mask conf.FinalMask
		if err := json.Unmarshal([]byte(fm), &mask); err == nil {
			sc.FinalMask = &mask
		}
	}

	switch getQuery(params, "none", "security") {
	case "tls", "xtls":
		sc.Security = "tls"

		alpnStr := getQuery(params, "", "alpn")
		var alpn *conf.StringList
		if alpnStr != "" {
			a := conf.NewStringList(strings.Split(alpnStr, ","))
			alpn = a
		}
		pcs := strings.NewReplacer("~", ",", ";", ",").Replace(
			getQuery(params, "", "pcs", "pinSHA256"),
		)
		sc.TLSSettings = &conf.TLSConfig{
			ServerName:           getQuery(params, "", "sni"),
			ALPN:                 alpn,
			Fingerprint:          getQuery(params, "chrome", "fp"),
			ECHConfigList:        getQuery(params, "", "ech"),
			PinnedPeerCertSha256: pcs,
		}
	case "reality":
		sc.Security = "reality"

		sc.REALITYSettings = &conf.REALITYConfig{
			ServerName:    getQuery(params, "", "sni"),
			ShortId:       getQuery(params, "", "sid"),
			Mldsa65Verify: getQuery(params, "", "pqv"),
			Fingerprint:   getQuery(params, "chrome", "fp"),
			SpiderX:       getQuery(params, "/", "spx"),
			Password:      getQuery(params, "", "pbk"),
		}
	default:
		sc.Security = "none"
	}

	switch getQuery(params, "raw", "type") {
	case "xhttp", "splithttp":
		sc.Network = ptr(conf.TransportProtocol("xhttp"))

		xhttp := &conf.SplitHTTPConfig{
			Host: getQuery(params, "", "host"),
			Path: getQuery(params, "", "path"),
			Mode: getQuery(params, "", "mode"),
		}
		if extra := params.Get("extra"); extra != "" {
			xhttp.Extra = json.RawMessage(extra)
		}
		sc.XHTTPSettings = xhttp
		sc.SplitHTTPSettings = xhttp

	case "mkcp", "kcp":
		sc.Network = ptr(conf.TransportProtocol("mkcp"))

		mtu, tti := uint32(1350), uint32(50)
		if m, err := strconv.ParseUint(getQuery(params, "", "mtu"), 10, 32); err == nil {
			mtu = uint32(m)
		}
		if t, err := strconv.ParseUint(getQuery(params, "", "tti"), 10, 32); err == nil {
			tti = uint32(t)
		}
		sc.KCPSettings = &conf.KCPConfig{
			Mtu: &mtu,
			Tti: &tti,
		}
		if sc.FinalMask == nil {
			settings, _ := json.Marshal(map[string]string{
				"header": getQuery(params, "", "header", "headerType"),
				"value":  getQuery(params, "", "value", "obfs-domain", "obfs-password", "seed"),
			})
			sc.FinalMask = &conf.FinalMask{
				Udp: []conf.Mask{
					{
						Type:     "mkcp-legacy",
						Settings: ptr(json.RawMessage(settings)),
					},
				},
			}
		}
	case "grpc":
		sc.Network = ptr(conf.TransportProtocol("grpc"))

		sc.GRPCSettings = &conf.GRPCConfig{
			ServiceName: getQuery(params, "", "serviceName"),
			Authority:   getQuery(params, "", "authority"),
			UserAgent:   getQuery(params, "", "user_agent", "userAgent"),
			MultiMode:   getQuery(params, "", "mode") == "multi",
		}

	case "httpupgrade", "websocket", "ws":
		if sc.TLSSettings != nil {
			sc.TLSSettings.ALPN = nil
		}
		path := getQuery(params, "/", "path")
		if !strings.Contains(path, "?ed=") {
			if ed := getQuery(params, "", "ed"); ed != "" {
				path += fmt.Sprintf("?ed=%s", ed)
			}
		}
		if getQuery(params, "", "type") == "httpupgrade" {
			sc.Network = ptr(conf.TransportProtocol("httpupgrade"))

			sc.HTTPUPGRADESettings = &conf.HttpUpgradeConfig{
				Path: path,
				Host: getQuery(params, "", "host"),
			}
		} else {
			sc.Network = ptr(conf.TransportProtocol("websocket"))

			sc.WSSettings = &conf.WebSocketConfig{
				Path: path,
				Host: getQuery(params, "", "host"),
			}
		}
	default:
		if u.Scheme() == "hy2" || u.Scheme() == "hysteria2" {
			sc.Network = ptr(conf.TransportProtocol("hysteria"))

			auth := ""
			if user := percentDecode(u.Username()); user != "" {
				if pass := percentDecode(u.Password()); pass != "" {
					auth = user + ":" + pass
				} else {
					auth = user
				}
			}
			sc.HysteriaSettings = &conf.HysteriaConfig{
				Version: 2,
				Auth:    auth,
			}
			if sc.FinalMask == nil {
				// salamander only
				sc.FinalMask = &conf.FinalMask{}
				if obfs := getQuery(params, "", "obfs"); obfs != "" {
					settings, _ := json.Marshal(
						map[string]string{
							"password": getQuery(params, "", "obfs-password"),
						},
					)
					sc.FinalMask.Udp = []conf.Mask{
						{
							Type:     getQuery(params, "", "obfs"),
							Settings: ptr(json.RawMessage(settings)),
						},
					}
				}
				if mport := getQuery(params, "", "mport"); mport != "" {
					fromStr, toStr, found := strings.Cut(mport, "-")
					// TODO поддержках mport с ,
					from, _ := strconv.Atoi(fromStr)
					to, _ := strconv.Atoi(toStr)
					if found {
						sc.FinalMask.QuicParams = &conf.QuicParamsConfig{
							UdpHop: conf.UdpHop{
								PortList: conf.PortList{
									Range: []conf.PortRange{
										{
											From: uint32(from),
											To:   uint32(to),
										},
									},
								},
							},
						}
					}
				}
			}
		} else {
			sc.Network = ptr(conf.TransportProtocol("raw"))

			sc.RAWSettings = &conf.TCPConfig{}
		}
	}

	return &sc, nil
}

func isExpectedSchemes(u *url.Url, schemes ...string) error {
	if !slices.Contains(schemes, u.Scheme()) {
		return fmt.Errorf("%q is not expected scheme", u.Scheme())
	}
	return nil
}

func commonConvert(u *url.Url, settings any, protocol, tag string) (*conf.OutboundDetourConfig, error) {
	strSettings, _ := json.Marshal(settings)
	ss, err := buildStreamSetting(u)
	if err != nil {
		return nil, err
	}
	return &conf.OutboundDetourConfig{
		Protocol:      protocol,
		Tag:           tag,
		Settings:      ptr(json.RawMessage(strSettings)),
		StreamSetting: ss,
	}, nil
}

func Vless(rawUrl, tag string) (*conf.OutboundDetourConfig, error) {
	u, err := url.Parse(rawUrl)
	if err != nil {
		return nil, err
	}
	if err := isExpectedSchemes(u, "vless"); err != nil {
		return nil, err
	}
	params := u.SearchParams()
	return commonConvert(
		u, map[string]any{
			"address":    u.Hostname(),
			"port":       u.DecodedPort(),
			"id":         percentDecode(u.Username()),
			"encryption": getQuery(params, "none", "encryption"),
			"flow":       getQuery(params, "", "flow"),
		}, "vless", tag,
	)
}

func Shadowsocks(rawUrl, tag string) (*conf.OutboundDetourConfig, error) {
	u, err := url.Parse(rawUrl)
	if err != nil {
		return nil, err
	}
	if err := isExpectedSchemes(u, "ss"); err != nil {
		return nil, err
	}
	var method, password, host string
	var port uint64
	_, b64, _ := strings.Cut(rawUrl, "://")
	b64, _, _ = strings.Cut(b64, "#")
	if d := utils.TryBase64(b64); d != nil {
		decoded := string(d)
		colon := strings.Index(decoded, ":")
		at := strings.LastIndex(decoded, "@")

		if colon == -1 || at == -1 || colon >= at {
			return nil, fmt.Errorf("invalid shadowsocks pre-SIP002 url")
		}
		method = decoded[:colon]
		password = decoded[colon+1 : at]

		colon = strings.LastIndex(decoded, ":")

		host = decoded[at+1 : colon]
		port, _ = strconv.ParseUint(decoded[colon+1:], 10, 16)
	} else if d := utils.TryBase64(percentDecode(u.Username())); d != nil {
		decoded := string(d)
		colon := strings.Index(decoded, ":")
		if colon == -1 {
			return nil, fmt.Errorf("invalid shadowsocks SIP002 url")
		}
		method = decoded[:colon]
		password = decoded[colon+1:]
		host = u.Hostname()
		port = uint64(u.DecodedPort())

	} else if u.Username() != "" && u.Password() != "" {
		method = percentDecode(u.Username())
		password = percentDecode(u.Password())
		host = u.Hostname()
		port = uint64(u.DecodedPort())
	} else {
		return nil, fmt.Errorf("invalid shadowsocks url")
	}
	return commonConvert(
		u, map[string]any{
			"address":  host,
			"port":     port,
			"method":   method,
			"password": password,
		}, "shadowsocks", tag,
	)
}

func Trojan(rawUrl, tag string) (*conf.OutboundDetourConfig, error) {
	u, err := url.Parse(rawUrl)
	if err != nil {
		return nil, err
	}
	if err := isExpectedSchemes(u, "trojan"); err != nil {
		return nil, err
	}
	u.SearchParams().Set("security", "tls")
	_, userinfo, _ := strings.Cut(rawUrl, "://")
	userinfo, _, found := strings.Cut(userinfo, "@")
	if !found {
		return nil, fmt.Errorf("invalid trojan url")
	}
	return commonConvert(
		u, map[string]any{
			"address":  u.Hostname(),
			"port":     u.DecodedPort(),
			"password": percentDecode(userinfo),
		}, "trojan", tag,
	)
}

func Vmess(rawUrl, tag string) (*conf.OutboundDetourConfig, error) {
	u, err := url.Parse(rawUrl)
	if err != nil {
		return nil, err
	}
	if err := isExpectedSchemes(u, "vmess"); err != nil {
		return nil, err
	}
	_, b64, _ := strings.Cut(rawUrl, "://")
	if d := utils.TryBase64(b64); d != nil {
		var vmess struct {
			V    *string `json:"v"`
			Add  string  `json:"add"`
			Port any     `json:"port"`
			Id   string  `json:"id"`
			Scy  *string `json:"scy"`
			Net  *string `json:"net"`
			Type *string `json:"type"`
			Host *string `json:"host"`
			Path *string `json:"path"`
			Tls  *string `json:"tls"`
			Sni  *string `json:"sni"`
			Alpn *string `json:"alpn"`
			Fp   *string `json:"fp"`
		}
		err := json.Unmarshal(d, &vmess)
		if err != nil {
			return nil, fmt.Errorf("invalid json in vmess")
		}
		/*
			if v := derefOr(vmess.V, "unknown"); v != "2" {
				return nil, fmt.Errorf("unsupported vmess version %q, only v2 is supported", v)
			}
		*/
		u, err = url.Parse(fmt.Sprintf("vmess://%s@%s:%v", vmess.Id, vmess.Add, vmess.Port))
		if err != nil {
			return nil, fmt.Errorf("invalid json parameters in vmess")
		}
		params := u.SearchParams()
		params.Set("security", derefOr(vmess.Tls, ""))
		params.Set("sni", derefOr(vmess.Sni, ""))
		params.Set("alpn", derefOr(vmess.Alpn, ""))
		params.Set("fp", derefOr(vmess.Fp, ""))
		params.Set("encryption", derefOr(vmess.Scy, ""))
		params.Set("type", derefOr(vmess.Net, ""))
		params.Set("headerType", derefOr(vmess.Type, ""))
		params.Set("host", derefOr(vmess.Host, ""))
		params.Set("path", derefOr(vmess.Path, ""))
		if vmess.Net != nil {
			switch *vmess.Net {
			case "kcp", "mkcp":
				params.Set("seed", derefOr(vmess.Path, ""))
			case "grpc":
				params.Set("serviceName", derefOr(vmess.Path, ""))
				params.Set("mode", derefOr(vmess.Type, ""))
				//			params.Delete("headerType")
			}
		}
	}
	params := u.SearchParams()
	return commonConvert(
		u, map[string]any{
			"address":     u.Hostname(),
			"port":        u.DecodedPort(),
			"id":          percentDecode(u.Username()),
			"security":    getQuery(params, "auto", "encryption"),
			"experiments": getQuery(params, "", "experiments"),
		}, "vmess", tag,
	)
}

func Http(rawUrl, tag string) (*conf.OutboundDetourConfig, error) {
	u, err := url.Parse(rawUrl)
	if err != nil {
		return nil, err
	}
	if err := isExpectedSchemes(u, "http", "https"); err != nil {
		return nil, err
	}
	if u.Scheme() == "https" {
		u.SearchParams().Set("security", "tls")
	}
	return commonConvert(
		u, map[string]any{
			"address": u.Hostname(),
			"port":    u.DecodedPort(),
			"user":    percentDecode(u.Username()),
			"pass":    percentDecode(u.Password()),
		}, "http", tag,
	)
}

func Socks5(rawUrl, tag string) (*conf.OutboundDetourConfig, error) {
	u, err := url.Parse(rawUrl)
	if err != nil {
		return nil, err
	}
	if err := isExpectedSchemes(u, "socks5"); err != nil {
		return nil, err
	}
	return commonConvert(
		u, map[string]any{
			"address": u.Hostname(),
			"port":    u.DecodedPort(),
			"user":    percentDecode(u.Username()),
			"pass":    percentDecode(u.Password()),
		}, "socks", tag,
	)
}

func Hysteria2(rawUrl, tag string) (*conf.OutboundDetourConfig, error) {
	u, err := url.Parse(rawUrl)
	if err != nil {
		return nil, err
	}
	if err := isExpectedSchemes(u, "hysteria2", "hy2"); err != nil {
		return nil, err
	}
	return commonConvert(
		u, map[string]any{
			"version": 2,
			"address": u.Hostname(),
			"port":    u.DecodedPort(),
		}, "hysteria", tag,
	)
}
