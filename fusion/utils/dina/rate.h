#ifndef __RATE_H__
#define __RATE_H__

#include <vector>
#include <time.h>

#ifdef __RATE_DEBUG
# include <stdio.h>
# include <stdlib.h>
# include <unistd.h>
#endif

class rate_t {
public:
  rate_t(time_t secs) {
    secs_ = (int)secs;
    nr_   = (int)secs + 2; // 1 extra for first period, 1 extra for last.
    samples_.reserve(nr_);
    reset();
  }

  ~rate_t() {}

  void reset() {
    time_t time_now = time(NULL);
    start_ident_    = (int)time_now/nr_;
    start_ix_       = time_now % nr_;

    samples_.clear();

    for (int i = 0; i < nr_; ++i) {
      samples_[i].ident = start_ident_;
      samples_[i].count = 0;
    }
  }

  void increment(int amount = 1) {
    time_t time_now = time(NULL);
    last_ident_     = (int)time_now / nr_;
    last_ix_        = time_now % nr_;

    if (samples_[last_ix_].ident != last_ident_) {
      samples_[last_ix_].ident = last_ident_;
      samples_[last_ix_].count = amount;
    }
    else
      samples_[last_ix_].count += amount;
  }

  float rate(); // in Hz, or amount per second

private:
  rate_t(rate_t const&);            // hide copy ctor
  rate_t& operator=(rate_t const&); // hide assignment operator

  struct sample_t {
    time_t  ident;
    int     count;
  };

  int secs_;
  int nr_;

  std::vector<sample_t> samples_;
  int start_ident_;
  int start_ix_;
  int last_ident_;
  int last_ix_;
};

float
rate_t::rate() {
  time_t time_now = time(NULL);
  int this_ident  = (int)time_now / nr_;
  int this_ix     = time_now % nr_;
  int check_ix    = this_ix;

#ifdef __RATE_DEBUG
  char* cmt;

  printf("Samples.....: %d\n", nr_);
  printf("Start ident.: %d\n", (int)start_ident_);
  printf("Start ix....: %d\n", (int)start_ix_);
  printf("Last ident..: %d\n", (int)last_ident_);
  printf("Last ix.....: %d\n", (int)last_ix_);
  printf("This ident..: %d\n", (int)this_ident);
  printf("This ix.....: %d\n", (int)this_ix);
#endif

  int total_count   = 0;
  int real_samples  = 0;

  while (real_samples < secs_) {
    if ((samples_[check_ix].ident == start_ident_ && check_ix == start_ix_) ||
        (samples_[check_ix].ident == this_ident   && check_ix == this_ix)) {
#ifdef __RATE_DEBUG
        cmt=(char*)"i";
#endif
    }
    else if (samples_[check_ix].ident == this_ident) {
      total_count += samples_[check_ix].count;
      real_samples++;
#ifdef __RATE_DEBUG
        cmt=(char*)"*";
#endif
    }
    else if (samples_[check_ix].ident == (this_ident-1) && check_ix == this_ix) {
#ifdef __RATE_DEBUG
      cmt=(char*)"i";
#endif
    }
    else {
      ++real_samples;
#ifdef __RATE_DEBUG
      cmt=(char*)"z";
#endif
    }

#ifdef __RATE_DEBUG
    printf("%s %8d %8d %8d\n", cmt, check_ix,(int)samples_[check_ix].ident, (int)samples_[check_ix].count);
#endif

    if (this_ident == start_ident_ && check_ix == start_ix_)
      break;

    if (check_ix == 0) {
      check_ix = nr_ - 1;
      --this_ident;
    }
    else
      --check_ix;

    if (check_ix == this_ix)
      break;
  }

  float f = real_samples ? (float)total_count/(float)real_samples : 0;

#ifdef __RATE_DEBUG
  printf("Real samples: %d\n",real_samples);
  printf("Frequency...: %f\n", f);
#endif

  return f;
}

#ifdef __RATE_DEBUG
int
main(int argc,char** argv) {
  int secs_       = 5;
  int time_limit  = 10;
  int sleep_time  = 0;

  if (argc > 1) {
    secs_      = atoi(argv[1]);
    time_limit = secs_ * 2;

    if (argc > 2) {
      time_limit = atoi(argv[2]);

      if (argc>3)
        sleep_time = atoi(argv[3]);
    }
  }

  rate_t fm(secs_);
  time_t start_time = time(NULL);

  printf("Seconds...: %d\n", secs_);
  printf("Time Limit: %d\n", time_limit);
  printf("Sleep time: %d\n", sleep_time);

  while (time(NULL) - start_time < time_limit) {
    fm.increment(1);

    if (sleep_time)
      sleep(sleep_time);
  }

  printf("%f\n", fm.rate());
}
#endif

#endif
