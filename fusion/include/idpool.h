/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef		FUSION_IDPOOL_H
#define		FUSION_IDPOOL_H

template <size_t POWER, size_t STEP, size_t RESERVED, size_t _BAD> struct idpool_t {
  typedef unsigned element_t;

  enum {
    BAD            = _BAD,
    MIN            = RESERVED,    // inclusive
    MAX            = 1 << POWER,  // exclusive
    BITS_PER_BYTE  = 8,
    ELEMENT_SIZE   = BITS_PER_BYTE * sizeof(element_t),
    ARRAY_SIZE     = (MAX / ELEMENT_SIZE) + ((MAX % ELEMENT_SIZE) != 0),
    CAPACITY       = MAX - MIN,
  };

private:
  // STATIC CHECKS
  bool __RESERVED_LESS_THEN_MAX[RESERVED < MAX];
  bool __BAD_WITHIN_MIN_MAX_RANGE[!((MIN <= BAD) && (BAD < MAX))];
  bool __STEP_NON_ZERO[STEP != 0];
  bool __STEP_BIGGEST[STEP <= (size_t)(1 << (32 - POWER))];
  bool __STEP_FOLD_MAX[!(MAX % STEP)];

  element_t bm_[ARRAY_SIZE];  // bit map
  size_t    pos_;             // starting position search for new 'id'
  size_t    nr_;              // number of ids allocated

  // valid range [MIN..MAX)

  bool _valid(size_t id) { /////////////////////////////////////////////////////
    return MIN <= id && id < MAX;
  }

  bool _free(size_t id) { //////////////////////////////////////////////////////
    return (bm_[id / ELEMENT_SIZE] & (1 << (id % ELEMENT_SIZE))) == 0;
  }

public:
  idpool_t() : pos_(MIN), nr_(0) { /////////////////////////////////////////////
    FUSION_DEBUG("BAD=%d MIN=%d MAX=%d BITS_PER_BYTE=%d ELEMENT_SIZE=%d ARRAY_SIZE=%d CAPACITY=%d",
      BAD,
      MIN,
      MAX,
      BITS_PER_BYTE,
      ELEMENT_SIZE,
      ARRAY_SIZE,
      CAPACITY
    );

    reset();
  }

  void reset() { ///////////////////////////////////////////////////////////////
    for (auto i = 0; i < ARRAY_SIZE; ++i)
      bm_[i] = 0;
  }

  size_t get(size_t hint) { ////////////////////////////////////////////////////
    if (!_valid(hint))
      FUSION_WARN("Invalid hint=%d", hint);

    if (_valid(hint) && _free(hint)) {
      bm_[hint / ELEMENT_SIZE] |= (1 << (hint % ELEMENT_SIZE));
      ++nr_;

      return hint;
    }

    return get();
  }

  size_t get() { ///////////////////////////////////////////////////////////////

    FUSION_ENSURE(nr_ <= CAPACITY, return BAD, "Pool exhausted");

    size_t id;

    do {
      id = (pos_ / MAX + pos_ % MAX) % MAX;
      pos_ += STEP;
    } while (!_valid(id) || !_free(id));

    bm_[id / ELEMENT_SIZE] |= 1 << (id % ELEMENT_SIZE);
    ++nr_;

    return id;
  }

  void put(size_t id) { ////////////////////////////////////////////////////////
    FUSION_ENSURE(_valid(id), return, "Invalid id=%d", id);
    FUSION_ENSURE(!_free(id), return, "Unallocated id=%d", id);

    bm_[id / ELEMENT_SIZE] &= ~(1 << (id % ELEMENT_SIZE));
    --nr_;
  }

  size_t nr() { ////////////////////////////////////////////////////////////////
    return nr_;
  }
};

#endif		//FUSION_IDPOOL_H
