# 6. Immutable data

Lists are good for sequences. For records, lookups and counting, Novelo has Maps and Sets. Like every value, they never change, so this chapter is also about how to work with data you cannot modify.

## Maps

A Map holds pairs of a key and a value, written in braces. Any value can be a key, but keywords are the usual choice for a record's fields:

```novelo
> (define ada {:name "Ada" :born 1815})
> (get ada :name)
"Ada"
> (contains? ada :born)
#true
> (count ada)
2
```

A missing key is an error for `get`, unless you pass a third argument, which is returned instead:

```novelo
> (define ada {:name "Ada" :born 1815})
> (get ada :died)
1:1: error :missing-key: get: the key is not in the map :died
> (get ada :died "unknown")
"unknown"
```

A map prints its pairs in the order the keys were first added, and `keys` lists them in that order:

```novelo
> (keys {:name "Ada" :born 1815})
(:name :born)
```

The keys and values of a map literal are evaluated, like the arguments of a call. To write a map exactly as it stands, quote it:

```novelo
> (define field :x)
> {field 1}
{:x 1}
> '{field 1}
{field 1}
```

## Changing a map without changing it

`associate` returns a map with one key added or replaced, and `dissociate` one with a key removed. The original stays as it was:

```novelo
> (define ada {:name "Ada" :born 1815})
> (associate ada :died 1852)
{:name "Ada" :born 1815 :died 1852}
> (dissociate ada :born)
{:name "Ada"}
> ada
{:name "Ada" :born 1815}
```

Making a new map for each change sounds expensive, but it is not. A map is stored as a tree, and the new map shares every part of it except the path to the key that changed. `associate` costs about the same as `get`, even for a map of a million keys ([core §8.6](../core-specification.md#86-large-data)). Such structures are called *persistent*.

Not being able to modify data has some practical effects:
- A function cannot change the map you give it, so you never need to copy a map "to be safe".
- Two parts of a program can share a value with no risk.
- Any old version you kept a name for is still there, unchanged.

The cost is that a "change" has to be passed along. A function that updates something returns the new version, and the caller uses that from then on.

## Accumulating into a map

The classic example is counting. Fold over the words with a map as the accumulator, and add one at each word:

```novelo
> (define count-words
>   (lambda (words)
>     (fold (lambda (counts word)
>             (associate counts word (+ 1 (get counts word 0))))
>           {} words)))
> (count-words '("a" "b" "a"))
{"a" 2 "b" 1}
```

`(get counts word 0)` reads 0 for a word not seen yet. Each step returns a new map, and `fold` passes it on to the next step.

## The library `map`

The library `map` ([stdlib §4.4](../standard-library.md#44-map)) works on whole maps:

```novelo
> (values {:a 1 :b 2})
(1 2)
> (entries {:a 1 :b 2})
((:a 1) (:b 2))
> (from-entries '((:a 1) (:b 2)))
{:a 1 :b 2}
> (merge {:a 1 :b 2} {:b 20 :c 30})
{:a 1 :b 20 :c 30}
> (map-values (lambda (n) (* n 10)) {:a 1 :b 2})
{:a 10 :b 20}
> (filter-entries (lambda (key value) (> value 1)) {:a 1 :b 2})
{:b 2}
> (fold-entries (lambda (total key value) (+ total value)) 0 {:a 1 :b 2})
3
```

`update` replaces a value with a function of it:

```novelo
> (update :born (lambda (year) (+ year 1)) {:name "Ada" :born 1815})
{:name "Ada" :born 1816}
```

## Nested data

Maps nest. To change a value deep inside, rebuild each level on the way out. `update` makes that readable:

```novelo
> (define person {:name "Ada" :address {:city "London"}})
> (get (get person :address) :city)
"London"
> (update :address (lambda (address) (associate address :city "Paris")) person)
{:name "Ada" :address {:city "Paris"}}
```

Only the maps on the path from the root to the change are rebuilt. The rest is shared.

## Sets

A Set holds distinct values, with no order to rely on and no repeats. It is written `#{…}`:

```novelo
> (define tags #{:new :sale})
> (contains? tags :sale)
#true
> (adjoin tags :featured)
#{:new :sale :featured}
> (disjoin tags :new)
#{:sale}
> (elements tags)
(:new :sale)
```

`adjoin` adds an element and `disjoin` removes one. `elements` gives them as a list. The library `set` ([stdlib §4.5](../standard-library.md#45-set)) has the operations from mathematics:

```novelo
> (from-list '(1 2 2 3 1))
#{1 2 3}
> (union #{1 2} #{2 3})
#{1 2 3}
> (intersection #{1 2} #{2 3})
#{2}
> (difference #{1 2} #{2 3})
#{1}
> (subset? #{1} #{1 2})
#true
```

`contains?` on a set costs at most a logarithm of its size, while finding an element in a list means walking through it. To ask "have I seen this?" many times, use a set.

## Equality of collections

`equal?` compares maps and sets by contents, regardless of order:

```novelo
> (equal? {:a 1 :b 2} {:b 2 :a 1})
#true
> (equal? #{1 2} #{2 1})
#true
```

Because values never change and Floats are always finite, any value can be a key or an element, including lists, maps and sets.

## Exercises

- **`06-inventory`.** Keep a stock of products in a map. Add, remove and value it, never changing a map in place.
- **`06-index`.** Build a map from each letter to the set of words that start with it, then answer questions with set operations.

```bash
tutorial/check.sh 06-inventory
```

The examples [`05-word-count`](../examples/05-word-count/main.nvl) and [`06-unique`](../examples/06-unique/main.nvl) count with maps and remove duplicates with sets.

Next: [Pattern matching](07-pattern-matching.md).
