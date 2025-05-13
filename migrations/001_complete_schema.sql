-- Tables from 001_create_tables.sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    username VARCHAR(50) UNIQUE NOT NULL,
    email VARCHAR(100) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE user_sessions (
    id SERIAL PRIMARY KEY,
    user_id INT REFERENCES users (id) ON DELETE CASCADE,
    token VARCHAR(255) UNIQUE NOT NULL,
    refresh_token VARCHAR(255) UNIQUE,
    device_info TEXT,
    ip_address VARCHAR(45),
    expires_at TIMESTAMP NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE orders (
    id SERIAL PRIMARY KEY,
    user_id INT REFERENCES users (id),
    status VARCHAR(50) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE posts (
    id SERIAL PRIMARY KEY,
    user_id INT REFERENCES users (id),
    content TEXT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    -- Fields from 002_enhance_posts.sql
    tags TEXT[] DEFAULT '{}',
    location VARCHAR(255),
    is_product_request BOOLEAN DEFAULT FALSE,
    request_status VARCHAR(50) DEFAULT 'open',
    price_range VARCHAR(100)
);

CREATE TABLE conversations (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE conversation_participants (
    conversation_id INT REFERENCES conversations (id) ON DELETE CASCADE,
    user_id INT REFERENCES users (id) ON DELETE CASCADE,
    PRIMARY KEY (conversation_id, user_id)
);

CREATE TABLE messages (
    id SERIAL PRIMARY KEY,
    conversation_id INT REFERENCES conversations (id) ON DELETE CASCADE,
    sender_id INT REFERENCES users (id) ON DELETE CASCADE,
    content TEXT NOT NULL,
    is_read BOOLEAN DEFAULT FALSE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    -- Fields from 004_negotiation_and_product_proof.sql
    context_type VARCHAR(50),
    context_id INT,
    metadata JSONB DEFAULT '{}'::jsonb
);

-- Table from 002_enhance_posts.sql
CREATE TABLE post_subscriptions (
    id SERIAL PRIMARY KEY,
    user_id INT REFERENCES users (id) ON DELETE CASCADE,
    post_id INT REFERENCES posts (id) ON DELETE CASCADE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (user_id, post_id)
);

-- Tables from 003_create_offers_table.sql
CREATE TABLE offers (
    id SERIAL PRIMARY KEY,
    post_id INT REFERENCES posts (id) ON DELETE CASCADE,
    user_id INT REFERENCES users (id) ON DELETE CASCADE,
    title VARCHAR(255) NOT NULL,
    description TEXT NOT NULL,
    price DECIMAL(10, 2),
    is_public BOOLEAN DEFAULT TRUE,
    status VARCHAR(50) DEFAULT 'pending',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    -- Fields from 004_negotiation_and_product_proof.sql
    negotiation_status VARCHAR(50) DEFAULT 'none',
    original_price DECIMAL(10, 2) NOT NULL
);

CREATE TABLE offer_notifications (
    id SERIAL PRIMARY KEY,
    offer_id INT REFERENCES offers (id) ON DELETE CASCADE,
    user_id INT REFERENCES users (id) ON DELETE CASCADE,
    is_read BOOLEAN DEFAULT FALSE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Tables from 004_negotiation_and_product_proof.sql
CREATE TABLE product_proofs (
    id SERIAL PRIMARY KEY,
    offer_id INT REFERENCES offers (id) ON DELETE CASCADE,
    user_id INT REFERENCES users (id) ON DELETE CASCADE,
    image_url TEXT NOT NULL,
    description TEXT,
    status VARCHAR(50) DEFAULT 'pending',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE price_negotiations (
    id SERIAL PRIMARY KEY,
    offer_id INT REFERENCES offers (id) ON DELETE CASCADE,
    user_id INT REFERENCES users (id) ON DELETE CASCADE,
    proposed_price DECIMAL(10, 2) NOT NULL,
    status VARCHAR(50) DEFAULT 'pending',
    message TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE escrow_transactions (
    id SERIAL PRIMARY KEY,
    offer_id INT REFERENCES offers (id) ON DELETE CASCADE,
    buyer_id INT REFERENCES users (id) ON DELETE CASCADE,
    seller_id INT REFERENCES users (id) ON DELETE CASCADE,
    amount DECIMAL(10, 2) NOT NULL,
    status VARCHAR(50) DEFAULT 'pending',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Indexes from 002_enhance_posts.sql
CREATE INDEX posts_tags_idx ON posts USING GIN (tags);

-- Indexes from 003_create_offers_table.sql
CREATE INDEX offers_post_id_idx ON offers (post_id);
CREATE INDEX offers_user_id_idx ON offers (user_id);
CREATE INDEX offer_notifications_user_id_idx ON offer_notifications (user_id);
CREATE INDEX offer_notifications_is_read_idx ON offer_notifications (is_read);

-- Indexes from 004_negotiation_and_product_proof.sql
CREATE INDEX product_proofs_offer_id_idx ON product_proofs (offer_id);
CREATE INDEX price_negotiations_offer_id_idx ON price_negotiations (offer_id);
CREATE INDEX escrow_transactions_offer_id_idx ON escrow_transactions (offer_id);
CREATE INDEX messages_context_idx ON messages (context_type, context_id);
