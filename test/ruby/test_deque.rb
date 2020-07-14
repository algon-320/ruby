# coding: US-ASCII
# frozen_string_literal: false
require 'test/unit'
require "delegate"
require "rbconfig/sizeof"

class TestDeque < Test::Unit::TestCase
  def setup
    @verbose = $VERBOSE
    $VERBOSE = nil
    @cls = Array
  end

  def teardown
    $VERBOSE = @verbose
  end

  def test_initialize
    d = Deque.new(3)
    assert_equal(3, d.size)
    assert_equal(">[nil, nil, nil]<", d.to_s)

    d = Deque.new(2, true)
    assert_equal(2, d.size)
    assert_equal(">[true, true]<", d.to_s)
  end

  def test_large
    for i in 0..10000 do
      d = Deque.new(1000, Deque.new(1000, 0))
    end
  end

  def test_inspect
    d = Deque.new
    assert_equal(">[]<", d.inspect)
    assert_equal(">[]<", d.to_s)
  end

  def test_length
    d = Deque.new
    assert_equal(0, d.length)
    assert_equal(0, d.size)
    d = Deque.new(4)
    assert_equal(4, d.size)
    d = Deque.new(65)
    assert_equal(65, d.size)
    d = Deque.new(65)
    assert_equal(65, d.size)
    d = Deque.new(1000)
    assert_equal(1000, d.size)
  end

  def test_push_back
    d = Deque.new
    d.push_back(123)
    assert_equal(1, d.size)
    assert_equal(">[123]<", d.to_s)
    
    for i in 4..6 do
      d.push_back(i);
    end
    assert_equal(4, d.size)
    assert_equal(">[123, 4, 5, 6]<", d.to_s)
    
    d = Deque.new
    for i in 1..100000 do
        d.push_back(i);
    end
    assert_equal(100000, d.size)
  end

  def test_push_front
    d = Deque.new
    d.push_front(123)
    assert_equal(1, d.size)
    assert_equal(">[123]<", d.to_s)
    
    for i in 4..6 do
      d.push_front(i);
    end
    assert_equal(4, d.size)
    assert_equal(">[6, 5, 4, 123]<", d.to_s)
    assert_equal(4, d.size)

    d = Deque.new
    for i in 1..100000 do
        d.push_front(i);
    end
    assert_equal(100000, d.size)
  end

  def test_at
    d = Deque.new(3, 123)
    assert_equal(123, d.at(0))
    assert_equal(123, d.at(1))
    assert_equal(123, d.at(2))
    assert_equal(123, d[2])
    d[0] = 456
    assert_equal(456, d[0])

    assert_equal(nil, d[10])
    assert_equal(123, d[-1])
    assert_equal(123, d[-2])
    assert_equal(456, d[-3])
    assert_equal(nil, d[-4])
    assert_equal(nil, d[-10])
  end

  def test_random
    d = Deque.new
    for i in 1..100000 do
      if rand(2) == 0
        d.push_front(i)
      else
        d.push_back(i)
      end
    end
    assert_equal(100000, d.size)
  end
end
