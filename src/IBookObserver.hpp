//
// Created by Simon Konieczny on 25/05/2026.
//
#pragma once

#ifndef LOB_IBOOKOBSERVER_HPP
#define LOB_IBOOKOBSERVER_HPP
#include <cstdint>
#include <chrono>

#endif

struct BookUpdate
{
    int64_t bidPrice, askPrice;
    uint32_t bidVol, askVol;
    uint64_t timestamp;
};

class IBookObserver
{
public:
    virtual ~IBookObserver() = default;
private:
    virtual void onBookUpdate(const BookUpdate&) = 0;
};