-- Add sample offers
INSERT INTO
    offers (
        post_id,
        user_id,
        title,
        description,
        price,
        is_public,
        status
    )
VALUES (
        3,
        2,
        'Premium Smartphone Offer',
        'I can offer you the latest Samsung Galaxy with 128GB storage and a 2-year warranty.',
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
        true,
        'pending'
    ),
    (
        4,
        1,
        'High-Performance Laptop',
        'Developer-focused laptop with 32GB RAM, 1TB SSD, and dedicated GPU.',
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
        true,
        'pending'
    ),
    (
        6,
        1,
        'Fitness Tracker Pro',
        'Latest model with heart rate monitoring, sleep tracking, and Android compatibility.',
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