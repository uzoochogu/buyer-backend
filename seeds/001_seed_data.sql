-- Passwords are hashed with Argon2id
-- 'password1' and 'password2' respectively
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

INSERT INTO posts (user_id, content) VALUES (1, 'First post!');

INSERT INTO posts (user_id, content) VALUES (2, 'Hello world!');

INSERT INTO chats (user_id, message) VALUES (1, 'Hi there!');

INSERT INTO chats (user_id, message) VALUES (2, 'Hello!');