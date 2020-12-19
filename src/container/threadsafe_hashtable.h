//
// Created by lnxterry on 9/12/20.
//

#ifndef MKR_MULTITHREAD_LIBRARY_THREADSAFE_HASHTABLE_H
#define MKR_MULTITHREAD_LIBRARY_THREADSAFE_HASHTABLE_H

#include "threadsafe_list.h"

namespace mkr {
    /**
     * Threadsafe hashtable.
     *
     * Invariants:
     * - The number of buckets do not change.
     * - Each key can only appear once in the hashtable at any given time.
     * - The same key will always be mapped to the same bucket.
     * - Each key can only be mapped to a single value.
     * - A key must have a value.
     * - When iterating from the head_ of a bucket, it will eventually lead to the tail.
     * - If the corresponding bucket does not contain the key, it does not exist.
     * - The same key must always return the same hash.
     *
     * Addtional Requirements:
     * - threadsafe_hashtable must be able to support type V where V is a non-copyable or non-movable type.
     * - threadsafe_hashtable does not have to support a non-copyable AND non-movable type.
     *
     * @tparam K The typename of the key.
     * @tparam V The typename of the value.
     * @tparam N The number of buckets in the hashtable. Prime numbers are highly recommended.
     */
    template<typename K, typename V, std::size_t N = 47>
    class threadsafe_hashtable {
    private:
        typedef std::shared_timed_mutex mutex_type;
        typedef std::unique_lock<mutex_type> writer_lock;
        typedef std::shared_lock<mutex_type> reader_lock;

        /**
         * A pair key-value pair.
         *
         * @warning The implicitly-declared or defaulted move constructor for class T is defined as deleted if any of the following is true:
         *          1. T has non-static data members that cannot be moved (have deleted, inaccessible, or ambiguous move constructors);
         *          2. T has direct or virtual base class that cannot be moved (has deleted, inaccessible, or ambiguous move constructors);
         *          3. T has direct or virtual base class with a deleted or inaccessible destructor;
         *          4. T is a union-like class and has a variant member with non-trivial move constructor.
         */
        struct pair {
        private:
            // Key.
            const K key_;
            // Value.
            std::shared_ptr<V> value_;

        public:
            pair(const K& _key, std::shared_ptr<V> _value)
                    :key_(_key), value_(_value) { }

            bool match_key(const K& _key) const { return key_==_key; }
            const K& get_key() const { return key_; }
            std::shared_ptr<V> get_value() { return value_; }
            std::shared_ptr<const V> get_value() const { return std::const_pointer_cast<const V>(value_); }
        };

        /**
         * Bucket containing a list of pairs.
         */
        struct bucket {
            mutable mutex_type mutex_;
            threadsafe_list<pair> list_;
        };

        // Buckets
        bucket buckets_[N];
        // Number of elements in the container.
        std::atomic_size_t num_elements_;

        /**
         * @param _key The key to hash to find the bucket.
         * @return The bucket the key belongs to.
         */
        const bucket& get_bucket(const K& _key) const
        {
            std::size_t hash = std::hash<K>{}(_key);
            return buckets_[hash%N];
        }

        /**
         * @param _key The key to hash to find the bucket.
         * @return The bucket the key belongs to.
         */
        bucket& get_bucket(const K& _key)
        {
            size_t hash = std::hash<K>{}(_key);
            return buckets_[hash%N];
        }

        /**
         * Internal function to add a key-value pair into the hashtable.
         * @param _key The key to add.
         * @param _value The value to add.
         * @return Returns true if key-value pair was added successfully. Returns false if key already exists in the hashtable.
         */
        bool do_insert(const K& _key, std::shared_ptr<V> _value)
        {
            std::function<bool(const pair&)> match_key = [&_key](const pair& _pair) { return _pair.match_key(_key); };

            // Lock bucket so that no other threads can add to the bucket at the same time.
            bucket& b = get_bucket(_key);
            writer_lock b_lock(b.mutex_);

            // If the bucket does not contain they key, add the new pair to the bucket and return true.
            if (b.list_.match_none(match_key)) {
                b.list_.push_front(pair(_key, _value));
                ++num_elements_;
                return true;
            }

            // Otherwise, if the bucket already contains the key, return false.
            return false;
        }

        /**
         * Internal function to replace an existing key-value pair in the hashtable.
         * @param _key The key to replace.
         * @param _value The new value to replace the old value.
         * @return Returns true if key-value pair was replaced successfully. Returns false if key does not already exists in the hashtable.
         */
        bool do_replace(const K& _key, std::shared_ptr<V> _value)
        {
            std::function<bool(const pair&)> match_key = [&_key](const pair& _pair) { return _pair.match_key(_key); };
            std::function<pair(void)> supplier = [_key, _value](void) { return pair(_key, _value); };

            // Lock bucket so that no other threads can add to the bucket at the same time.
            bucket& b = get_bucket(_key);
            writer_lock b_lock(b.mutex_);

            // Return true if the pair was successfully replace, otherwise, return false.
            return b.list_.replace_if(match_key, supplier);
        }

    public:
        /**
         * Constructs the hashtable.
         */
        threadsafe_hashtable()
                :num_elements_(0) { }

        /**
         * Destructs the hashtable.
         */
        ~threadsafe_hashtable() = default;

        threadsafe_hashtable(const threadsafe_hashtable&) = delete;
        threadsafe_hashtable(threadsafe_hashtable&&) = delete;
        threadsafe_hashtable operator=(const threadsafe_hashtable&) = delete;
        threadsafe_hashtable operator=(threadsafe_hashtable&&) = delete;

        /**
         * Add a key-value pair into the hashtable.
         * @param _key The key to add.
         * @param _value The value to add.
         * @return Returns true if key-value pair was added successfully. Returns false if key already exists in the hashtable.
         */
        bool insert(const K& _key, const V& _value)
        {
            return do_insert(_key, std::make_shared<V>(_value));
        }

        /**
         * Add a key-value pair into the hashtable.
         * @param _key The key to add.
         * @param _value The value to add.
         * @return Returns true if key-value pair was added successfully. Returns false if key already exists in the hashtable.
         */
        bool insert(const K& _key, V&& _value)
        {
            return do_insert(_key, std::make_shared<V>(std::forward<V>(_value)));
        }

        /**
         * Replace an existing key-value pair in the hashtable.
         * @param _key The key of the pair to replace.
         * @param _value The new value to replace the old value.
         * @return Returns true if key-value pair was replaced successfully. Returns false if key does not already exists in the hashtable.
         */
        bool replace(const K& _key, const V& _value)
        {
            return do_replace(_key, std::make_shared<V>(_value));
        }

        /**
         * Replace an existing key-value pair in the hashtable.
         * @param _key The key of the pair to replace.
         * @param _value The new value to replace the old value.
         * @return Returns true if key-value pair was replaced successfully. Returns false if key does not already exists in the hashtable.
         */
        bool replace(const K& _key, V&& _value)
        {
            return do_replace(_key, std::make_shared<V>(std::forward<V>(_value)));
        }

        /**
         * Remove an existing key-value pair in the hashtable.
         * @param _key The key of the pair to remove.
         * @return Returns true if key-value pair was removed successfully. Returns false if key does not already exists in the hashtable.
         */
        bool remove(const K& _key)
        {
            std::function<bool(const pair&)> match_key = [&_key](const pair& _pair) { return _pair.match_key(_key); };

            // Step 2: Lock bucket
            bucket& b = get_bucket(_key);
            writer_lock b_lock(b.mutex_);

            if (b.list_.remove_if(match_key)) {
                --num_elements_;
                return true;
            }
            return false;
        }

        /**
         * Returns the value that belongs to the key.
         * @param _key The key that the value belongs to.
         * @return Returns the value that belongs to the key. If the key does not exist in the hashtable, nullptr is returned.
         */
        std::shared_ptr<V> at(const K& _key)
        {
            std::function<bool(const pair&)> match_key = [&_key](const pair& _pair) { return _pair.match_key(_key); };

            bucket& b = get_bucket(_key);
            reader_lock b_lock(b.mutex_);

            std::shared_ptr<pair> p = b.list_.find_first_if(match_key);
            return p ? p->get_value() : nullptr;
        }

        /**
         * Returns the value that belongs to the key.
         * @param _key The key that the value belongs to.
         * @return Returns the value that belongs to the key. If the key does not exist in the hashtable, nullptr is returned.
         */
        std::shared_ptr<const V> at(const K& _key) const
        {
            std::function<bool(const pair&)> match_key = [&_key](const pair& _pair) { return _pair.match_key(_key); };

            const bucket& b = get_bucket(_key);
            reader_lock b_lock(b.mutex_);

            std::shared_ptr<const pair> p = b.list_.find_first_if(match_key);
            return p ? p->get_value() : nullptr;
        }

        /**
         * Perform the mapper operation on a key-value pair.
         * @tparam return_type The return type of the mapper function.
         * @param _key The key of the pair to perform the mapper function on.
         * @param _mapper The mapper function to perform on the key-value pair.
         * @return The result of the mapper function.
         */
        template<typename return_type>
        std::optional<return_type> write_and_map(const K& _key, std::function<return_type(V&)> _mapper)
        {
            std::function<bool(const pair&)> match_key = [&_key](const pair& _pair) { return _pair.match_key(_key); };

            bucket& b = get_bucket(_key);
            writer_lock b_lock(b.mutex_);

            std::shared_ptr<pair> p = b.list_.find_first_if(match_key);
            return p ? std::optional<return_type>(_mapper(*p->get_value())) : std::nullopt;
        }

        /**
         * Perform the mapper operation on a key-value pair.
         * @tparam return_type The return type of the mapper function.
         * @param _key The key of the pair to perform the mapper function on.
         * @param _mapper The mapper function to perform on the key-value pair.
         * @return The result of the mapper function.
         */
        template<typename return_type>
        std::optional<return_type> read_and_map(const K& _key, std::function<return_type(const V&)> _mapper) const
        {
            std::function<bool(const pair&)> match_key = [&_key](const pair& _pair) { return _pair.match_key(_key); };

            const bucket& b = get_bucket(_key);
            writer_lock b_lock(b.mutex_);

            std::shared_ptr<const pair> p = b.list_.find_first_if(match_key);
            return p ? std::optional<return_type>(_mapper(*p->get_value())) : std::nullopt;
        }

        /**
         * Checks if the hashtable contains the key.
         * @param _key The key to check.
         * @return True if the hashtable contains the key, else false.
         */
        bool has(const K& _key) const
        {
            std::function<bool(const pair&)> match_key = [&_key](const pair& _pair) { return _pair.match_key(_key); };

            const bucket& b = get_bucket(_key);
            reader_lock b_lock(b.mutex_);

            return b.list_.match_any(match_key);
        }

        /**
         * Clear the hashtable.
         */
        void clear()
        {
            writer_lock b_locks[N];
            for (size_t i = 0; i<N; i++) {
                b_locks[i] = std::move(writer_lock(buckets_[i].mutex_));
                buckets_[i].list_.clear();
            }
            num_elements_ = 0;
        }

        /**
         * Checks if the container is empty.
         * @return Returns true if the container is empty, false otherwise.
         */
        bool empty() const { return num_elements_.load()==0; }

        /**
         * Returns the number of elements in the container.
         * @return Returns the number of elements in the container.
         */
        std::size_t size() const { return num_elements_.load(); }
    };
}

#endif //MKR_MULTITHREAD_LIBRARY_THREADSAFE_HASHTABLE_H