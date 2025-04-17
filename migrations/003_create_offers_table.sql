-- Create offers table
CREATE TABLE offers (
    id SERIAL PRIMARY KEY,
    post_id INT REFERENCES posts (id) ON DELETE CASCADE,
    user_id INT REFERENCES users (id) ON DELETE CASCADE,
    title VARCHAR(255) NOT NULL,
    description TEXT NOT NULL,
    price DECIMAL(10, 2),
    is_public BOOLEAN DEFAULT TRUE,
    status VARCHAR(50) DEFAULT 'pending', -- pending, accepted, rejected, expired
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Create offer_notifications table
CREATE TABLE offer_notifications (
    id SERIAL PRIMARY KEY,
    offer_id INT REFERENCES offers (id) ON DELETE CASCADE,
    user_id INT REFERENCES users (id) ON DELETE CASCADE,
    is_read BOOLEAN DEFAULT FALSE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Add indexes for performance
CREATE INDEX offers_post_id_idx ON offers (post_id);

CREATE INDEX offers_user_id_idx ON offers (user_id);

CREATE INDEX offer_notifications_user_id_idx ON offer_notifications (user_id);

CREATE INDEX offer_notifications_is_read_idx ON offer_notifications (is_read);