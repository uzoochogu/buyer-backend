-- Users from 001_seed_data.sql
INSERT INTO
    users (
        username,
        email,
        password_hash
    )
VALUES (
        'user1',
        'user1@example.com',
        '$argon2id$v=19$m=65536,t=3,p=1$AAECAwQFBgcICQoLDA0ODw$bzDH4P6BAWTzmkyK9gG8DhHIl5RPE/ceQ2vxmhLMrOs'
    );

INSERT INTO
    users (
        username,
        email,
        password_hash
    )
VALUES (
        'user2',
        'user2@example.com',
        '$argon2id$v=19$m=65536,t=3,p=1$AAECAwQFBgcICQoLDA0ODw$dRSOf3JvhtzfrAc6DRPwKD+0I6oq9eaPNWcy7CCMeYM'
    );

-- Orders from 001_seed_data.sql
INSERT INTO orders (user_id, status) VALUES (1, 'in_progress');
INSERT INTO orders (user_id, status) VALUES (2, 'delivered');
INSERT INTO orders (user_id, status) VALUES (1, 'pending');
INSERT INTO orders (user_id, status) VALUES (2, 'in_progress');
INSERT INTO orders (user_id, status) VALUES (1, 'cancelled');
INSERT INTO orders (user_id, status) VALUES (2, 'delivered');
INSERT INTO orders (user_id, status) VALUES (1, 'pending');
INSERT INTO orders (user_id, status) VALUES (2, 'in_transit');
INSERT INTO orders (user_id, status) VALUES (1, 'delivered');
INSERT INTO orders (user_id, status) VALUES (2, 'pending');

-- Basic posts from 001_seed_data.sql
INSERT INTO posts (user_id, content) VALUES (1, 'First post!');
INSERT INTO posts (user_id, content) VALUES (2, 'Hello world!');

-- Update posts with enhanced fields from 002_enhanced_seed_data.sql
UPDATE posts
SET
    tags = ARRAY['general', 'announcement'],
    location = NULL,
    is_product_request = false,
    request_status = 'open',
    price_range = NULL
WHERE
    id = 1;

UPDATE posts
SET
    tags = ARRAY['general', 'introduction'],
    location = NULL,
    is_product_request = false,
    request_status = 'open',
    price_range = NULL
WHERE
    id = 2;

-- Add product request posts from 002_enhanced_seed_data.sql
INSERT INTO
    posts (
        user_id,
        content,
        tags,
        location,
        is_product_request,
        request_status,
        price_range
    )
VALUES (
        1,
        'Looking for a high-quality smartphone with good camera capabilities. Any recommendations?',
        ARRAY[
            'electronics',
            'smartphone',
            'camera'
        ],
        'New York',
        true,
        'open',
        '$500-$800'
    );

INSERT INTO
    posts (
        user_id,
        content,
        tags,
        location,
        is_product_request,
        request_status,
        price_range
    )
VALUES (
        2,
        'Need recommendations for a reliable laptop for programming and design work.',
        ARRAY[
            'electronics',
            'laptop',
            'programming'
        ],
        'San Francisco',
        true,
        'open',
        '$1000-$1500'
    );

INSERT INTO
    posts (
        user_id,
        content,
        tags,
        location,
        is_product_request,
        request_status,
        price_range
    )
VALUES (
        1,
        'Looking for organic skincare products, preferably locally made.',
        ARRAY[
            'beauty',
            'skincare',
            'organic'
        ],
        'Los Angeles',
        true,
        'open',
        '$20-$100'
    );

INSERT INTO
    posts (
        user_id,
        content,
        tags,
        location,
        is_product_request,
        request_status,
        price_range
    )
VALUES (
        2,
        'Can anyone recommend a good fitness tracker that works with Android?',
        ARRAY[
            'fitness',
            'wearable',
            'android'
        ],
        'Chicago',
        true,
        'in_progress',
        '$50-$150'
    );

-- Post subscriptions from 002_enhanced_seed_data.sql
INSERT INTO post_subscriptions (user_id, post_id) VALUES (1, 3);
INSERT INTO post_subscriptions (user_id, post_id) VALUES (1, 4);
INSERT INTO post_subscriptions (user_id, post_id) VALUES (2, 3);
INSERT INTO post_subscriptions (user_id, post_id) VALUES (2, 5);
INSERT INTO post_subscriptions (user_id, post_id) VALUES (1, 6);

-- Sample conversations from 001_seed_data.sql
INSERT INTO conversations (name) VALUES ('User1 and User2');

-- Add participants to the conversation
INSERT INTO
    conversation_participants (conversation_id, user_id)
VALUES (1, 1);

INSERT INTO
    conversation_participants (conversation_id, user_id)
VALUES (1, 2);

-- Sample messages from 001_seed_data.sql
INSERT INTO
    messages (
        conversation_id,
        sender_id,
        content
    )
VALUES (1, 1, 'Hi there!');

INSERT INTO
    messages (
        conversation_id,
        sender_id,
        content
    )
VALUES (1, 2, 'Hello! How are you?');

INSERT INTO
    messages (
        conversation_id,
        sender_id,
        content
    )
VALUES (
        1,
        1,
        'I''m doing well, thanks for asking!'
    );

INSERT INTO
    messages (
        conversation_id,
        sender_id,
        content
    )
VALUES (1, 2, 'Great to hear that!');

-- Sample offers from 003_offer_seed_data.sql (fixed to include original_price)
INSERT INTO
    offers (
        post_id,
        user_id,
        title,
        description,
        price,
        original_price,
        is_public,
        status
    )
VALUES (
        3,
        2,
        'Premium Smartphone Offer',
        'I can offer you the latest Samsung Galaxy with 128GB storage and a 2-year warranty.',
        699.99,
        699.99,
        true,
        'pending'
    ),
    (
        3,
        1,
        'Budget Smartphone Option',
        'I have a great mid-range phone with excellent camera for half the price of flagships.',
        349.99,
        349.99,
        true,
        'pending'
    ),
    (
        4,
        1,
        'High-Performance Laptop',
        'Developer-focused laptop with 32GB RAM, 1TB SSD, and dedicated GPU.',
        1299.99,
        1299.99,
        true,
        'pending'
    ),
-- Continuing from where we left off
    (
        4,
        1,
        'High-Performance Laptop',
        'Developer-focused laptop with 32GB RAM, 1TB SSD, and dedicated GPU.',
        1299.99,
        1299.99,
        true,
        'pending'
    ),
    (
        5,
        2,
        'Organic Skincare Bundle',
        'Complete set of locally-made organic skincare products with natural ingredients.',
        89.99,
        89.99,
        true,
        'pending'
    ),
    (
        6,
        1,
        'Fitness Tracker Pro',
        'Latest model with heart rate monitoring, sleep tracking, and Android compatibility.',
        129.99,
        129.99,
        false,
        'pending'
    );

-- Add notifications for these offers
INSERT INTO
    offer_notifications (offer_id, user_id, is_read)
VALUES (1, 1, false),
    (2, 1, false),
    (3, 2, false),
    (4, 1, false),
    (5, 2, false);

-- Add sample product proofs
INSERT INTO
    product_proofs (offer_id, user_id, image_url, description, status)
VALUES 
    (1, 2, 'https://example.com/images/galaxy_proof1.jpg', 'Front view of the Samsung Galaxy', 'approved'),
    (1, 2, 'https://example.com/images/galaxy_proof2.jpg', 'Back view showing camera system', 'approved'),
    (3, 1, 'https://example.com/images/laptop_proof1.jpg', 'Laptop with specs shown on screen', 'pending'),
    (4, 2, 'https://example.com/images/skincare_proof1.jpg', 'Complete skincare set in packaging', 'pending');

-- Add sample price negotiations
INSERT INTO
    price_negotiations (offer_id, user_id, proposed_price, status, message)
VALUES
    (1, 1, 650.00, 'pending', 'Would you consider $650 for the smartphone?'),
    (1, 2, 675.00, 'pending', 'I can go down to $675, but that''s my best offer.'),
    (3, 2, 1200.00, 'pending', 'Can you do $1200 for the laptop?'),
    (4, 1, 75.00, 'accepted', 'Would $75 work for the skincare bundle?');

-- Add sample escrow transactions
INSERT INTO
    escrow_transactions (offer_id, buyer_id, seller_id, amount, status)
VALUES
    (1, 1, 2, 675.00, 'pending'),
    (4, 1, 2, 75.00, 'completed');
