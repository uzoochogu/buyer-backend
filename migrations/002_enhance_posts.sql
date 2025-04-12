-- Extend posts table with product tags, location, and request status
ALTER TABLE posts
ADD COLUMN tags TEXT [] DEFAULT '{}',
ADD COLUMN location VARCHAR(255),
ADD COLUMN is_product_request BOOLEAN DEFAULT FALSE,
ADD COLUMN request_status VARCHAR(50) DEFAULT 'open',
ADD COLUMN price_range VARCHAR(100);

-- Create a subscriptions table for users to follow posts
CREATE TABLE post_subscriptions (
    id SERIAL PRIMARY KEY,
    user_id INT REFERENCES users (id) ON DELETE CASCADE,
    post_id INT REFERENCES posts (id) ON DELETE CASCADE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (user_id, post_id)
);

-- Create an index for faster tag-based searches
CREATE INDEX posts_tags_idx ON posts USING GIN (tags);