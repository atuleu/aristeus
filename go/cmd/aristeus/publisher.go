package main

import (
	"errors"
	"fmt"
	"sync"
)

type Publisher[T any] struct {
	mx            sync.RWMutex
	subscriptions map[<-chan T]chan T
}

func (p *Publisher[T]) Subscribe(initialValues []T) <-chan T {
	res := make(chan T, len(initialValues))
	p.mx.Lock()
	defer p.mx.Unlock()

	if p.subscriptions == nil {
		p.subscriptions = make(map[<-chan T]chan T)
	}

	p.subscriptions[res] = res
	for _, v := range initialValues {
		res <- v
	}
	return res
}

func (p *Publisher[T]) Unsubscribe(ch <-chan T) error {
	p.mx.Lock()
	defer p.mx.Unlock()

	actual, ok := p.subscriptions[ch]
	if ok == false {
		return fmt.Errorf("object is not managed")
	}

	delete(p.subscriptions, ch)
	close(actual)
	return nil
}

func (p *Publisher[T]) Update(value T) error {
	p.mx.RLock()
	defer p.mx.RUnlock()
	var errs []error
	for id, ch := range p.subscriptions {
		select {
		case ch <- value:
			//OK, do nothing.
		default:
			errs = append(errs, fmt.Errorf("subscription %p would block", id))
		}
	}
	return errors.Join(errs...)
}
