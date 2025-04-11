-- Update existing posts with enhanced fields
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

-- Add new product request posts
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

-- Add some post subscriptions
INSERT INTO post_subscriptions (user_id, post_id) VALUES (1, 3);

INSERT INTO post_subscriptions (user_id, post_id) VALUES (1, 4);

INSERT INTO post_subscriptions (user_id, post_id) VALUES (2, 3);

INSERT INTO post_subscriptions (user_id, post_id) VALUES (2, 5);

INSERT INTO post_subscriptions (user_id, post_id) VALUES (1, 6);