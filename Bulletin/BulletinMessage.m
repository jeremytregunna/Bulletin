//
//  BulletinMessage.m
//  Conduit
//
//  Created by Jeremy Tregunna on 2013-05-12.
//  Copyright (c) 2013 Jeremy Tregunna. All rights reserved.
//

#import "BulletinMessage.h"

@implementation BulletinMessage

+ (instancetype)messageWithMessageID:(uint16_t)messageID payload:(NSData*)payload topic:(NSString*)topic qualityOfService:(BulletinMessageServiceLevel)qos retained:(BOOL)retained
{
    return [[self alloc] initWithMessageID:messageID payload:payload topic:topic qualityOfService:qos retained:retained];
}

- (instancetype)initWithMessageID:(uint16_t)messageID payload:(NSData*)payload topic:(NSString*)topic qualityOfService:(BulletinMessageServiceLevel)qos retained:(BOOL)retained
{
    NSParameterAssert(qos >= 0 && qos <= 2);

    if((self = [super init]))
    {
        _messageID = messageID;
        _payload = [payload copy];
        _topic = [topic copy];
        _qualityOfService = qos;
        _retained = retained;
    }

    return self;
}

#pragma mark - Secure coding

+ (BOOL)supportsSecureCoding
{
    return YES;
}

- (id)initWithCoder:(NSCoder*)decoder
{
    if((self = [super init]))
    {
        _messageID = (uint16_t)[decoder decodeIntForKey:@"messageID"];
        _topic = [decoder decodeObjectOfClass:[NSString class] forKey:@"topic"];
        _payload = [decoder decodeObjectOfClass:[NSData class] forKey:@"payload"];
        _qualityOfService = (BulletinMessageServiceLevel)[decoder decodeIntForKey:@"qos"];
        _retained = [decoder decodeBoolForKey:@"retained"];
    }
    return self;
}

- (void)encodeWithCoder:(NSCoder*)coder
{
    [coder encodeInt:_messageID forKey:@"messageID"];
    [coder encodeObject:_topic forKey:@"topic"];
    [coder encodeObject:_payload forKey:@"payload"];
    [coder encodeInt:_qualityOfService forKey:@"qos"];
    [coder encodeBool:_retained forKey:@"retained"];
}

#pragma mark - Copying

- (id)copyWithZone:(NSZone*)zone
{
    typeof(self) result = [[[self class] alloc] initWithMessageID:self.messageID payload:self.payload topic:self.topic qualityOfService:self.qualityOfService retained:self.retained];
    return result;
}

- (BOOL)isEqual:(BulletinMessage*)other
{
    return _messageID == other.messageID && [_payload isEqualToData:other.payload] && [_topic isEqualToString:other.topic] && _qualityOfService == other.qualityOfService && _retained == other.retained;
}

- (NSUInteger)hash
{
    NSUInteger prime = 31;
    NSUInteger result = 1;

    result = prime * result + _messageID;
    result = prime * result + [_payload hash];
    result = prime * result + [_topic hash];
    result = prime * result + _qualityOfService;
    result = prime * result + _retained;

    return result;
}

@end
