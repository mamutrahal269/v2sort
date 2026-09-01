package main

import (
	"fmt"
	"io"
	"math/rand"
	"net/http"
	"os"
	"regexp"
	"strings"
	"time"

	"github.com/mamutrahal269/v2sort/utils"
)

func fetchFromURL(rawUrl, userAgent, hwid string, timeout time.Duration) ([]string, error) {
	req, _ := http.NewRequest("GET", rawUrl, nil)

	if userAgent != "" {
		req.Header.Set("User-Agent", userAgent)
	} else {
		req.Header.Set("User-Agent", "Happ/3.26.3")
	}
	if hwid != "" {
		req.Header.Set("X-HWID", hwid)
	} else {
		req.Header.Set("X-HWID", fmt.Sprintf("%016x", rand.Uint64()))
	}

	client := &http.Client{Timeout: timeout}
	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	data, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, err
	}
	if d := utils.TryBase64(string(data)); d != nil {
		data = d
	}

	re := regexp.MustCompile(`(?m)(?:^|\s)([\pL][\pL\pN+.-]*)://(.+)$`)
	matches := re.FindAllString(string(data), -1)
	result := make([]string, len(matches))
	for i, m := range matches {
		result[i] = strings.TrimSpace(m)
	}
	return result, nil
}

func fetchFromFile(path string) ([]string, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	if d := utils.TryBase64(string(data)); d != nil {
		data = d
	}

	re := regexp.MustCompile(`(?m)(?:^|\s)([\pL][\pL\pN+.-]*)://(.+)$`)

	matches := re.FindAllString(string(data), -1)
	result := make([]string, len(matches))
	for i, m := range matches {
		result[i] = strings.TrimSpace(m)
	}
	return result, nil
}

func fetchAll(sources []string, userAgent, hwid string, fetchTimeout time.Duration) ([]string, error) {
	list := make([]string, 0, 0)
	for _, s := range sources {
		if strings.Contains(s, "://") {
			l, err := fetchFromURL(s, userAgent, hwid, fetchTimeout)
			if err != nil {
				fmt.Fprintf(os.Stderr, "не удалось получить ключи из %q: %v\n", s, err)
				continue
			}
			list = append(list, l...)
		} else {
			l, err := fetchFromFile(s)
			if err != nil {
				fmt.Fprintf(os.Stderr, "не удалось получить ключи из %q: %v\n", s, err)
				continue
			}
			list = append(list, l...)
		}
	}
	if len(list) == 0 {
		return nil, fmt.Errorf("не найдено ни одного ключа")
	}
	return list, nil
}
