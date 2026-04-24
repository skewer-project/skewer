package api

import (
	"net/http"
	"sync"
	"time"

	"golang.org/x/time/rate"
)

// EmailLimiter enforces two token buckets per authenticated email:
//   - init: protects signed-URL issuance (relatively cheap, allow more)
//   - submit: protects pipeline starts (expensive, allow fewer)
//
// Stale entries are evicted after IdleTTL of no activity.
type EmailLimiter struct {
	initPerHour   int
	submitPerHour int
	idleTTL       time.Duration

	mu      sync.Mutex
	buckets map[string]*userBuckets
}

type userBuckets struct {
	init     *rate.Limiter
	submit   *rate.Limiter
	lastSeen time.Time
}

func NewEmailLimiter(initPerHour, submitPerHour int) *EmailLimiter {
	return &EmailLimiter{
		initPerHour:   initPerHour,
		submitPerHour: submitPerHour,
		idleTTL:       2 * time.Hour,
		buckets:       map[string]*userBuckets{},
	}
}

func (l *EmailLimiter) getBuckets(email string) *userBuckets {
	l.mu.Lock()
	defer l.mu.Unlock()
	b, ok := l.buckets[email]
	if !ok {
		b = &userBuckets{
			init:   rate.NewLimiter(rate.Limit(float64(l.initPerHour)/3600.0), l.initPerHour),
			submit: rate.NewLimiter(rate.Limit(float64(l.submitPerHour)/3600.0), l.submitPerHour),
		}
		l.buckets[email] = b
	}
	b.lastSeen = time.Now()
	return b
}

// AllowInit returns true if an /init call by the given email is permitted.
func (l *EmailLimiter) AllowInit(email string) bool {
	return l.getBuckets(email).init.Allow()
}

// AllowSubmit returns true if a /submit call by the given email is permitted.
func (l *EmailLimiter) AllowSubmit(email string) bool {
	return l.getBuckets(email).submit.Allow()
}

// Sweep removes idle buckets. Call periodically.
func (l *EmailLimiter) Sweep() {
	l.mu.Lock()
	defer l.mu.Unlock()
	cutoff := time.Now().Add(-l.idleTTL)
	for email, b := range l.buckets {
		if b.lastSeen.Before(cutoff) {
			delete(l.buckets, email)
		}
	}
}

// Tooling: middleware that rejects requests with 429 when a limiter says no.
// `pick` chooses which bucket (init vs submit) to consult.
func (l *EmailLimiter) Middleware(pick func(r *http.Request) func(string) bool) func(http.Handler) http.Handler {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			email := EmailFromContext(r.Context())
			if email == "" {
				next.ServeHTTP(w, r)
				return
			}
			allow := pick(r)
			if allow != nil && !allow(email) {
				httpError(w, http.StatusTooManyRequests, "rate limit exceeded")
				return
			}
			next.ServeHTTP(w, r)
		})
	}
}
