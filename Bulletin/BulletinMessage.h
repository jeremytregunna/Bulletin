//
//  BulletinMessage.h
//  Conduit
//
//  Created by Jeremy Tregunna on 2013-05-12.
//  Copyright (c) 2013 Jeremy Tregunna. All rights reserved.
//

#import <Foundation/Foundation.h>

typedef NS_ENUM(uint8_t, BulletinMessageServiceLevel)
{
    // Fast: Delivered once, with no confirmation required
    BulletinMessageServiceLevelNone = 0,
    // Slow: Delivered at least once, requiring confirmation
    BulletinMessageServiceLevelConfirmation,
    // Slowest: Delivered at most once, using a four step handshake
    BulletinMessageServiceLevelHandshake
};

@interface BulletinMessage : NSObject <NSCopying, NSSecureCoding>
@property (nonatomic) uint16_t messageID;
@property (nonatomic, copy) NSString* topic;
@property (nonatomic, copy) NSData* payload;
@property (nonatomic) BulletinMessageServiceLevel qualityOfService;
@property (nonatomic) BOOL retained;

+ (instancetype)messageWithMessageID:(uint16_t)messageID payload:(NSData*)payload topic:(NSString*)topic qualityOfService:(BulletinMessageServiceLevel)qos retained:(BOOL)retained;
@end
