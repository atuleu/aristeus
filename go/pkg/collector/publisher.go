package collector

import (
	"context"
	"errors"
	"fmt"
	"sync"
	"time"
)

var (
	ErrSubscriptionNotManaged = errors.New("subscription is not managed")
	ErrSubscriptionWouldBlock = errors.New("subscription would block")
)

const SubscriptionStuckThreshold = 30

type subscription[T any] struct {
	ch     chan T
	missed chan T

	incoming *chan T
	mx       sync.Mutex

	backlogCtx    context.Context
	cancelBacklog context.CancelFunc
	backlogDone   chan struct{}
	backlog       []T
}

func newSubscription[T any](backlog []T, timeout time.Duration) *subscription[T] {
	res := &subscription[T]{
		ch:      make(chan T, 64),
		backlog: backlog,
	}
	if len(backlog) == 0 {
		return res
	}

	res.missed = make(chan T, 128)
	res.incoming = &res.missed
	res.backlogCtx, res.cancelBacklog = context.WithTimeout(context.Background(), timeout)
	res.backlogDone = make(chan struct{})
	go res.pushBacklog()

	return res
}

func (s *subscription[T]) pushBacklog() {
	defer s.cancelBacklog()
	defer close(s.backlogDone)

	for _, v := range s.backlog {
		select {
		case <-s.backlogCtx.Done():
			return
		case s.ch <- v:
		}
	}

	blocked := false
	// best effort drain most missed value to avoid out-of-order
	for !blocked {
		select {
		case v := <-s.missed:
			select {
			case s.ch <- v:
			case <-s.backlogCtx.Done():
				return
			}
		default:
			blocked = true
		}
	}
	// small delay to avoid switching to a filled up queue.
	time.Sleep(50 * time.Millisecond)
	// switches an closes atomically, We avoid any send to a closed channel
	s.mx.Lock()
	s.incoming = &s.ch
	close(s.missed)
	s.mx.Unlock()

	// Maybe we still had some misses, even if we drained before hand. Better
	// out-of-order than a miss.
	for v := range s.missed {
		select {
		case s.ch <- v:
		case <-s.backlogCtx.Done():
			return
		}
	}
}

func (s *subscription[T]) Close() {
	if s.missed != nil {
		s.cancelBacklog()
		<-s.backlogDone
	}
	close(s.ch)
}

func (s *subscription[T]) backlogIsDone() bool {
	select {
	case <-s.backlogDone:
		return true
	default:
		return false
	}
}

func (s *subscription[T]) push(value T) error {
	if s.missed == nil || s.backlogIsDone() == true {
		select {
		case s.ch <- value:
			return nil
		default:
			return ErrSubscriptionWouldBlock
		}
	}

	// we need a lock to send as backlog may switch and close.
	s.mx.Lock()
	defer s.mx.Unlock()

	select {
	case *s.incoming <- value:
		return nil
	default:
		return ErrSubscriptionWouldBlock
	}
}

type Publisher[T any] struct {
	mx            sync.RWMutex
	subscriptions map[<-chan T]*subscription[T]
}

func (p *Publisher[T]) Subscribe(backlog []T, backlogTimeout time.Duration) <-chan T {
	p.mx.Lock()
	defer p.mx.Unlock()
	res := newSubscription[T](backlog, backlogTimeout)

	if p.subscriptions == nil {
		p.subscriptions = make(map[<-chan T]*subscription[T])
	}
	p.subscriptions[res.ch] = res
	return res.ch
}

func (p *Publisher[T]) Unsubscribe(ch <-chan T) error {
	p.mx.Lock()
	actual, ok := p.subscriptions[ch]
	if ok == true {
		delete(p.subscriptions, ch)
	}
	p.mx.Unlock()

	if ok == false {
		return ErrSubscriptionNotManaged
	}

	actual.Close()
	return nil
}

func (p *Publisher[T]) UpdateOne(ch <-chan T, value T) error {
	p.mx.RLock()
	defer p.mx.RUnlock()

	actual, ok := p.subscriptions[ch]
	if ok == false {
		return ErrSubscriptionNotManaged
	}

	return actual.push(value)
}

func (p *Publisher[T]) Update(value T) error {
	p.mx.RLock()
	defer p.mx.RUnlock()
	var errs []error
	for id, s := range p.subscriptions {
		if err := s.push(value); err != nil {
			go p.Unsubscribe(id)
			errs = append(errs, fmt.Errorf("subscription %p: %w", id, err))
		}
	}
	return errors.Join(errs...)
}
