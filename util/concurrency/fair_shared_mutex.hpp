#ifndef BOOST_THREAD_PTHREAD_FAIR_SHARED_MUTEX_HPP
#define BOOST_THREAD_PTHREAD_FAIR_SHARED_MUTEX_HPP

// (C) Copyright 2006-8 Anthony Williams
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)

#include <boost/assert.hpp>
#include <boost/static_assert.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/detail/thread_interruption.hpp>

#include <boost/config/abi_prefix.hpp>

namespace boost
{
    class fair_shared_mutex
    {
    private:
        struct state_data
        {
            unsigned shared_count;
            unsigned waiting_exclusive;
            unsigned waiting_shared;
            bool exclusive;
            bool upgrade;
            bool exclusive_waiting_blocked;
        };



        state_data state;
        boost::mutex state_change;
        boost::condition_variable shared_cond;
        boost::condition_variable exclusive_cond;
        boost::condition_variable upgrade_cond;

        void release_waiters()
        {
            exclusive_cond.notify_one();
            shared_cond.notify_all();
        }

        void release_waiters_from_shared()
        {
            if (state.waiting_exclusive)
            {
                exclusive_cond.notify_one();
            }    
        }

        void release_waiters_from_exclusive()
        {
            if (state.waiting_shared)
            {
                if (state.waiting_exclusive)
                {
                    shared_cond.notify_one();
                }
                else
                {
                    shared_cond.notify_all();
                }
            }
            else
            {
                exclusive_cond.notify_one();
            }
        }

    public:
        fair_shared_mutex()
        {
            state_data state_={0,0,0,0,0,0};
            state=state_;
        }

        ~fair_shared_mutex()
        {
        }

        void lock_shared()
        {
            boost::this_thread::disable_interruption do_not_disturb;
            boost::mutex::scoped_lock lk(state_change);

            ++state.waiting_shared;
            while(state.exclusive || state.exclusive_waiting_blocked)
            {
                shared_cond.wait(lk);
            }
            if (state.waiting_exclusive)
            {
                state.exclusive_waiting_blocked = true;
            }
            --state.waiting_shared;
            ++state.shared_count;
        }

        bool try_lock_shared()
        {
            boost::mutex::scoped_lock lk(state_change);

            if(state.exclusive || state.exclusive_waiting_blocked)
            {
                return false;
            }
            else
            {
                ++state.shared_count;
                return true;
            }
        }

        bool timed_lock_shared(system_time const& timeout)
        {
            boost::this_thread::disable_interruption do_not_disturb;
            boost::mutex::scoped_lock lk(state_change);

            ++state.waiting_shared;
            while(state.exclusive || state.exclusive_waiting_blocked)
            {
                if(!shared_cond.timed_wait(lk,timeout))
                {
                    --state.waiting_shared;
                    return false;
                }
            }
            --state.waiting_shared;
            ++state.shared_count;
            return true;
        }

        template<typename TimeDuration>
        bool timed_lock_shared(TimeDuration const & relative_time)
        {
            return timed_lock_shared(get_system_time()+relative_time);
        }

        void unlock_shared()
        {
            boost::mutex::scoped_lock lk(state_change);
            bool const last_reader=!--state.shared_count;

            if(last_reader)
            {
                if(state.upgrade)
                {
                    state.upgrade=false;
                    state.exclusive=true;
                    upgrade_cond.notify_one();
                }
                else
                {
                    state.exclusive_waiting_blocked=false;
                }
                release_waiters_from_shared();
            }
        }

        void lock()
        {
            boost::this_thread::disable_interruption do_not_disturb;
            boost::mutex::scoped_lock lk(state_change);
        
            ++state.waiting_exclusive;
            while(state.shared_count || state.exclusive)
            {
                state.exclusive_waiting_blocked=true;
                exclusive_cond.wait(lk);
            }
            --state.waiting_exclusive;
            state.exclusive=true;
        }

        bool timed_lock(system_time const& timeout)
        {
            boost::this_thread::disable_interruption do_not_disturb;
            boost::mutex::scoped_lock lk(state_change);

            ++state.waiting_exclusive;
            while(state.shared_count || state.exclusive)
            {
                state.exclusive_waiting_blocked=true;
                if(!exclusive_cond.timed_wait(lk,timeout))
                {
                    --state.waiting_exclusive;
                    if(state.shared_count || state.exclusive)
                    {
                        state.exclusive_waiting_blocked=false;
                        release_waiters();
                        return false;
                    }
                    break;
                }
            }
            --state.waiting_exclusive;
            state.exclusive=true;
            return true;
        }

        template<typename TimeDuration>
        bool timed_lock(TimeDuration const & relative_time)
        {
            return timed_lock(get_system_time()+relative_time);
        }

        bool try_lock()
        {
            boost::mutex::scoped_lock lk(state_change);

            if(state.shared_count || state.exclusive)
            {
                return false;
            }
            else
            {
                state.exclusive=true;
                return true;
            }

        }

        void unlock()
        {
            boost::mutex::scoped_lock lk(state_change);
            state.exclusive=false;
            state.exclusive_waiting_blocked=false;
            release_waiters_from_exclusive();
        }

        void lock_upgrade()
        {
            boost::this_thread::disable_interruption do_not_disturb;
            boost::mutex::scoped_lock lk(state_change);
            while(state.exclusive || state.exclusive_waiting_blocked || state.upgrade)
            {
                shared_cond.wait(lk);
            }
            ++state.shared_count;
            state.upgrade=true;
        }

        bool timed_lock_upgrade(system_time const& timeout)
        {
            boost::this_thread::disable_interruption do_not_disturb;
            boost::mutex::scoped_lock lk(state_change);
            while(state.exclusive || state.exclusive_waiting_blocked || state.upgrade)
            {
                if(!shared_cond.timed_wait(lk,timeout))
                {
                    if(state.exclusive || state.exclusive_waiting_blocked || state.upgrade)
                    {
                        return false;
                    }
                    break;
                }
            }
            ++state.shared_count;
            state.upgrade=true;
            return true;
        }

        template<typename TimeDuration>
        bool timed_lock_upgrade(TimeDuration const & relative_time)
        {
            return timed_lock_upgrade(get_system_time()+relative_time);
        }

        bool try_lock_upgrade()
        {
            boost::mutex::scoped_lock lk(state_change);
            if(state.exclusive || state.exclusive_waiting_blocked || state.upgrade)
            {
                return false;
            }
            else
            {
                ++state.shared_count;
                state.upgrade=true;
                return true;
            }
        }

        void unlock_upgrade()
        {
            boost::mutex::scoped_lock lk(state_change);
            state.upgrade=false;
            bool const last_reader=!--state.shared_count;

            if(last_reader)
            {
                state.exclusive_waiting_blocked=false;
                release_waiters();
            }
        }

        void unlock_upgrade_and_lock()
        {
            boost::this_thread::disable_interruption do_not_disturb;
            boost::mutex::scoped_lock lk(state_change);
            --state.shared_count;
            while(state.shared_count)
            {
                upgrade_cond.wait(lk);
            }
            state.upgrade=false;
            state.exclusive=true;
        }

        void unlock_and_lock_upgrade()
        {
            boost::mutex::scoped_lock lk(state_change);
            state.exclusive=false;
            state.upgrade=true;
            ++state.shared_count;
            state.exclusive_waiting_blocked=false;
            release_waiters();
        }

        void unlock_and_lock_shared()
        {
            boost::mutex::scoped_lock lk(state_change);
            state.exclusive=false;
            ++state.shared_count;
            state.exclusive_waiting_blocked=false;
            release_waiters();
        }

        void unlock_upgrade_and_lock_shared()
        {
            boost::mutex::scoped_lock lk(state_change);
            state.upgrade=false;
            state.exclusive_waiting_blocked=false;
            release_waiters();
        }
    };
}

#include <boost/config/abi_suffix.hpp>

#endif
